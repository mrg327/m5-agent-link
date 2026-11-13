//! The agent graph (GRAPH.md, proto 0.22): per session, the current turn's tool calls as nodes on lanes.
//! Lane 0 is the session's own agent; each sub-agent forks a lane 1-7 and merges back when it stops.
//! Fed with the raw hook lines (`hook_source`), keyed like the registry (a sub-agent's lines land on its
//! parent's key). No agent content is kept: a node is a kind, a state and its lane.

use crate::hook_source::{session_key, tool_args, tool_ok};
use crate::registry::Via;
use agentlink_proto::Graph;
use serde_json::Value;
use std::collections::{HashMap, VecDeque};

/// The newest calls a graph keeps; older ones are counted in `drop`.
pub const MAX_NODES: usize = 64;
/// Sessions tracked at once; the least recently fed one goes first.
const MAX_SESSIONS: usize = 64;
/// The highest lane; when every lane is taken, further sub-agents share it.
const TOP_LANE: u8 = 7;

// Kinds (GRAPH.md §1).
pub const OTHER: u64 = 0;
pub const READ: u64 = 1;
pub const EDIT: u64 = 2;
pub const SHELL: u64 = 3;
pub const SEARCH: u64 = 4;
pub const WEB: u64 = 5;
pub const AGENT: u64 = 6;
pub const ASK: u64 = 7;
pub const PLAN: u64 = 8;
pub const GIT: u64 = 9;
pub const TEST: u64 = 10;
pub const MCP: u64 = 11;

// States.
pub const RUNNING: u64 = 0;
pub const OK: u64 = 1;
pub const FAILED: u64 = 2;

/// The node's kind from its tool name (compared without case) and, for a shell call, its command.
pub fn kind_of(tool: &str, payload: &Value) -> u64 {
    let t = tool.to_ascii_lowercase();
    match t.as_str() {
        "view" | "read" | "read_file" | "notebookread" => READ,
        "edit" | "create" | "write" | "multiedit" | "notebookedit" | "apply_patch" => EDIT,
        "bash" | "powershell" | "write_bash" | "read_bash" | "stop_bash" | "bashoutput"
        | "killshell" => shell_kind(payload),
        "grep" | "glob" | "ls" | "lsp" => SEARCH,
        "web_fetch" | "webfetch" | "websearch" | "web_search" => WEB,
        "task" | "agent" | "read_agent" | "write_agent" | "list_agents" => AGENT,
        "ask_user" | "askuserquestion" => ASK,
        "todowrite" | "sql" | "update_todo" | "report_intent" | "exitplanmode"
        | "enterplanmode" => PLAN,
        _ if t.ends_with("code_search") => SEARCH,
        // Claude Code names MCP tools `mcp__server__tool`, Copilot CLI `server-tool`
        _ if t.starts_with("mcp__") || t.contains('-') => MCP,
        _ => OTHER,
    }
}

/// A shell call: git (the command starts with `git` or `gh`), a test run (turn.rs), or plain shell.
fn shell_kind(payload: &Value) -> u64 {
    let args = tool_args(payload);
    let Some(cmd) = args["command"].as_str() else {
        return SHELL;
    };
    match cmd.split_whitespace().next() {
        Some("git" | "gh") => GIT,
        _ if crate::turn::recognise(cmd).is_some() => TEST,
        _ => SHELL,
    }
}

/// The lane kind (`la`) from `agentType` / `agent_type`, compared without case.
pub fn lane_kind(agent_type: &str) -> u64 {
    match agent_type.to_ascii_lowercase().as_str() {
        "" => 0,
        "explore" => 1,
        "task" => 2,
        "general-purpose" => 3,
        "code-review" => 4,
        "research" => 5,
        "plan" => 6,
        _ => 7,
    }
}

/// The sub-agent a hook line comes from, as (id, type): Copilot CLI's `parentAgentId` differing from
/// `sessionId` (id `agentId`), or Claude Code's `agent_id`. `None` for the session's own agent.
fn sub_agent(p: &Value) -> Option<(String, String)> {
    let nonempty = |k: &str| p[k].as_str().filter(|s| !s.is_empty());
    if let Some(parent) = nonempty("parentAgentId") {
        let sid = nonempty("sessionId");
        if sid != Some(parent) {
            let id = nonempty("agentId").or(sid).unwrap_or("?");
            return Some((
                id.to_owned(),
                nonempty("agentType").unwrap_or("").to_owned(),
            ));
        }
        return None;
    }
    let id = nonempty("agent_id")?;
    Some((
        id.to_owned(),
        nonempty("agent_type").unwrap_or("").to_owned(),
    ))
}

#[derive(Debug, Clone)]
struct Node {
    kind: u64,
    state: u64,
    lane: u8,
    fork: bool,
    merge: bool,
    /// `toolCallId` / `tool_use_id`, to pair the result with its call.
    id: Option<String>,
    tool: String,
}

impl Node {
    fn bits(&self) -> u64 {
        self.kind
            | self.state << 4
            | u64::from(self.lane) << 6
            | u64::from(self.fork) << 9
            | u64::from(self.merge) << 10
    }
}

#[derive(Default)]
struct SessionGraph {
    nodes: VecDeque<Node>,
    drop: u64,
    gen: u64,
    /// Last use, for eviction.
    tick: u64,
    /// Open sub-agents and their lanes.
    agents: HashMap<String, u8>,
    la: [u64; 8],
}

impl SessionGraph {
    fn lane_of(&mut self, sub: Option<&(String, String)>) -> (u8, bool) {
        let Some((id, ty)) = sub else {
            return (0, false);
        };
        if let Some(l) = self.agents.get(id) {
            return (*l, false);
        }
        let lane = (1..=TOP_LANE)
            .find(|l| !self.agents.values().any(|x| x == l))
            .unwrap_or(TOP_LANE);
        if !self.agents.values().any(|x| *x == lane) {
            self.la[lane as usize] = lane_kind(ty);
        }
        self.agents.insert(id.clone(), lane);
        (lane, true)
    }

    fn push(&mut self, node: Node) {
        self.nodes.push_back(node);
        while self.nodes.len() > MAX_NODES {
            self.nodes.pop_front();
            self.drop += 1;
        }
    }

    /// Settles the call `id`, else the newest running call of `tool` in `lane`. Whether one was found.
    fn settle(&mut self, id: Option<&str>, tool: &str, lane: Option<u8>, state: u64) -> bool {
        let hit = match id {
            Some(id) => self
                .nodes
                .iter_mut()
                .rev()
                .find(|n| n.id.as_deref() == Some(id)),
            None => None,
        };
        let hit = match (hit, lane) {
            (Some(n), _) => Some(n),
            (None, Some(lane)) => self
                .nodes
                .iter_mut()
                .rev()
                .find(|n| n.state == RUNNING && n.lane == lane && n.tool == tool),
            (None, None) => None,
        };
        match hit {
            Some(n) if n.state != state => {
                n.state = state;
                true
            }
            _ => false,
        }
    }

    /// Closes `lane`: its last node gets the merge bit. A shared lane closes with its last sub-agent.
    fn merge_lane(&mut self, lane: u8) {
        if self.agents.values().any(|l| *l == lane) {
            return;
        }
        if let Some(n) = self.nodes.iter_mut().rev().find(|n| n.lane == lane) {
            n.merge = true;
        }
    }

    fn stop_agent(&mut self, id: &str) -> bool {
        let Some(lane) = self.agents.remove(id) else {
            return false;
        };
        self.merge_lane(lane);
        true
    }

    /// The parent's turn ended: every open lane merges, and a call still running never got its result
    /// (denied at its prompt, interrupted), so it failed.
    fn stop_all(&mut self) -> bool {
        let lanes: Vec<u8> = self.agents.drain().map(|(_, l)| l).collect();
        for l in &lanes {
            self.merge_lane(*l);
        }
        let mut settled = false;
        for n in self.nodes.iter_mut().filter(|n| n.state == RUNNING) {
            n.state = FAILED;
            settled = true;
        }
        settled || !lanes.is_empty()
    }

    fn reset(&mut self) -> bool {
        let had = !self.nodes.is_empty() || self.drop > 0 || !self.agents.is_empty();
        self.nodes.clear();
        self.drop = 0;
        self.agents.clear();
        self.la = [0; 8];
        had
    }
}

/// Every tracked session's graph.
#[derive(Default)]
pub struct Graphs {
    sessions: HashMap<String, SessionGraph>,
    tick: u64,
    gen: u64,
}

impl Graphs {
    /// Feeds one raw hook line. Returns whether a graph changed.
    pub fn on_line(&mut self, v: &Value, via: &Via) -> bool {
        let (Some(key), Some(event)) = (session_key(v, via), v["event"].as_str()) else {
            return false;
        };
        let event = event.to_ascii_lowercase();
        let p = &v["payload"];
        let sub = sub_agent(p);
        if event == "sessionend" {
            return sub.is_none() && self.sessions.remove(&key).is_some();
        }
        let tool = p["toolName"]
            .as_str()
            .or_else(|| p["tool_name"].as_str())
            .unwrap_or("");
        let id = p["toolCallId"]
            .as_str()
            .or_else(|| p["tool_use_id"].as_str())
            .filter(|s| !s.is_empty());
        self.tick += 1;
        let tick = self.tick;
        let changed = if event == "pretooluse" {
            if !self.sessions.contains_key(&key) && self.sessions.len() >= MAX_SESSIONS {
                let oldest = self.sessions.iter().min_by_key(|(_, g)| g.tick);
                if let Some(k) = oldest.map(|(k, _)| k.clone()) {
                    self.sessions.remove(&k);
                }
            }
            let g = self.sessions.entry(key.clone()).or_default();
            let (lane, fork) = g.lane_of(sub.as_ref());
            g.push(Node {
                kind: kind_of(tool, p),
                state: RUNNING,
                lane,
                fork,
                merge: false,
                id: id.map(str::to_owned),
                tool: tool.to_owned(),
            });
            true
        } else {
            let Some(g) = self.sessions.get_mut(&key) else {
                return false;
            };
            match event.as_str() {
                "posttooluse" | "posttoolusefailure" => {
                    let lane = match &sub {
                        None => Some(0),
                        Some((a, _)) => g.agents.get(a).copied(),
                    };
                    let state = if tool_ok(&event, p) { OK } else { FAILED };
                    g.settle(id, tool, lane, state)
                }
                "userpromptsubmit" | "userpromptsubmitted" if sub.is_none() => g.reset(),
                "stop" | "agentstop" | "subagentstop" => match &sub {
                    Some((a, _)) => g.stop_agent(a),
                    None if event == "subagentstop" => false,
                    None => g.stop_all(),
                },
                "stopfailure" | "erroroccurred" if sub.is_none() => g.stop_all(),
                _ => false,
            }
        };
        if let Some(g) = self.sessions.get_mut(&key) {
            g.tick = tick;
            if changed {
                self.gen += 1;
                g.gen = self.gen;
            }
        }
        changed
    }

    /// Session `key`'s graph as the `graph` message for `sid`; an empty one when nothing is known.
    pub fn graph(&self, key: &str, sid: u64) -> Graph {
        let Some(g) = self.sessions.get(key) else {
            return Graph {
                sid,
                nodes: vec![],
                la: None,
                drop: None,
                gen: Some(0),
            };
        };
        let top = g.nodes.iter().map(|n| n.lane).max().unwrap_or(0) as usize;
        Graph {
            sid,
            nodes: g.nodes.iter().map(Node::bits).collect(),
            la: (top > 0).then(|| g.la[..=top].to_vec()),
            drop: (g.drop > 0).then_some(g.drop),
            gen: Some(g.gen),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    const FORK: u64 = 1 << 9;
    const MERGE: u64 = 1 << 10;

    fn node(kind: u64, state: u64, lane: u64) -> u64 {
        kind | state << 4 | lane << 6
    }

    fn feed(g: &mut Graphs, agent: &str, event: &str, payload: Value) -> bool {
        g.on_line(
            &json!({"agent": agent, "event": event, "payload": payload}),
            &Via::Local,
        )
    }

    fn cp(g: &mut Graphs, event: &str, payload: Value) -> bool {
        feed(g, "copilot", event, payload)
    }

    fn cl(g: &mut Graphs, event: &str, payload: Value) -> bool {
        feed(g, "claude", event, payload)
    }

    /// A Copilot CLI call of the parent session `p`: pre then post.
    fn cp_call(g: &mut Graphs, id: &str, tool: &str, args: Value) {
        let pre = json!({"sessionId": "p", "toolCallId": id, "toolName": tool, "toolArgs": args});
        cp(g, "preToolUse", pre.clone());
        let mut post = pre;
        post["toolResult"] = json!({"resultType": "success"});
        cp(g, "postToolUse", post);
    }

    /// A Copilot CLI sub-agent's (agentId `a`) tool hook payload, as the CLI sends it.
    fn cp_sub(a: &str, ty: &str, id: &str, tool: &str) -> Value {
        json!({"agentId": a, "agentType": ty, "cwd": "/r", "parentAgentId": "p", "sessionId": a,
               "toolCallId": id, "toolName": tool, "toolArgs": {"command": "ls"}})
    }

    fn graph(g: &Graphs, key: &str) -> Graph {
        g.graph(key, 9)
    }

    #[test]
    fn kinds_follow_the_table_without_case() {
        let none = json!({});
        let cases = [
            ("view", READ),
            ("Read", READ),
            ("NotebookRead", READ),
            ("apply_patch", EDIT),
            ("MultiEdit", EDIT),
            ("create", EDIT),
            ("bash", SHELL),
            ("KillShell", SHELL),
            ("LS", SEARCH),
            ("lsp", SEARCH),
            ("semantic_code_search", SEARCH),
            ("WebFetch", WEB),
            ("web_search", WEB),
            ("Task", AGENT),
            ("read_agent", AGENT),
            ("AskUserQuestion", ASK),
            ("ask_user", ASK),
            ("TodoWrite", PLAN),
            ("report_intent", PLAN),
            ("ExitPlanMode", PLAN),
            ("mcp__github__get_issue", MCP),
            ("microsoft-playwright-mcp-browser_click", MCP),
            ("fetch_copilot_cli_documentation", OTHER),
            ("", OTHER),
        ];
        for (tool, kind) in cases {
            assert_eq!(kind_of(tool, &none), kind, "{tool}");
        }
    }

    #[test]
    fn a_shell_call_is_git_or_test_by_its_command() {
        let claude = |c: &str| json!({"tool_input": {"command": c}});
        let copilot_str = |c: &str| json!({"toolArgs": json!({"command": c}).to_string()});
        assert_eq!(kind_of("Bash", &claude("git status")), GIT);
        assert_eq!(kind_of("bash", &copilot_str("  gh pr view 7")), GIT);
        assert_eq!(kind_of("powershell", &copilot_str("git log -1")), GIT);
        assert_eq!(kind_of("Bash", &claude("cargo test -p x")), TEST);
        assert_eq!(kind_of("bash", &copilot_str("cd d && npm test")), TEST);
        assert_eq!(kind_of("bash", &claude("gitk")), SHELL);
        assert_eq!(kind_of("bash", &claude("echo git")), SHELL);
        assert_eq!(
            kind_of("read_bash", &json!({"toolArgs": {"shellId": "1"}})),
            SHELL
        );
    }

    #[test]
    fn lane_kinds_without_case() {
        for (t, k) in [
            ("", 0),
            ("Explore", 1),
            ("task", 2),
            ("general-purpose", 3),
            ("code-review", 4),
            ("research", 5),
            ("Plan", 6),
            ("my-reviewer", 7),
        ] {
            assert_eq!(lane_kind(t), k, "{t}");
        }
    }

    #[test]
    fn copilot_parent_and_background_sub_agents() {
        let mut g = Graphs::default();
        let key = "copilot:p";
        cp_call(&mut g, "t1", "view", json!({"path": "/r/a"}));
        // the parent starts two background sub-agents: its task calls end at once
        cp_call(&mut g, "t2", "task", json!({"agent_type": "explore"}));
        cp_call(&mut g, "t3", "task", json!({"agent_type": "code-review"}));
        cp(&mut g, "preToolUse", cp_sub("a1", "explore", "s1", "grep"));
        cp(
            &mut g,
            "preToolUse",
            cp_sub("a2", "code-review", "s2", "bash"),
        );
        // a sub-agent's own prompt does not start a new turn
        assert!(!cp(
            &mut g,
            "userPromptSubmitted",
            json!({"sessionId": "a1", "parentAgentId": "p", "agentId": "a1", "prompt": "x"})
        ));
        let mut post = cp_sub("a1", "explore", "s1", "grep");
        post["toolResult"] = json!({"resultType": "success"});
        cp(&mut g, "postToolUse", post);
        let mut post = cp_sub("a2", "code-review", "s2", "bash");
        post["toolResult"] = json!({"resultType": "failure"});
        cp(&mut g, "postToolUse", post);
        // a1 stops; a2 still works
        cp(
            &mut g,
            "agentStop",
            json!({"sessionId": "a1", "parentAgentId": "p", "agentId": "a1", "agentType": "explore"}),
        );
        cp(
            &mut g,
            "preToolUse",
            cp_sub("a2", "code-review", "s3", "view"),
        );
        let out = graph(&g, key);
        assert_eq!(
            out.nodes,
            vec![
                node(READ, OK, 0),
                node(AGENT, OK, 0),
                node(AGENT, OK, 0),
                node(SEARCH, OK, 1) | FORK | MERGE,
                node(SHELL, FAILED, 2) | FORK,
                node(READ, RUNNING, 2),
            ]
        );
        assert_eq!(out.la, Some(vec![0, 1, 4]));
        assert_eq!(out.drop, None);
        // a new sub-agent takes the freed lane 1
        cp(
            &mut g,
            "preToolUse",
            cp_sub("a3", "research", "s4", "web_fetch"),
        );
        let out = graph(&g, key);
        assert_eq!(*out.nodes.last().unwrap(), node(WEB, RUNNING, 1) | FORK);
        assert_eq!(out.la, Some(vec![0, 5, 4]));
        // the parent's turn ends: every open lane merges
        cp(&mut g, "agentStop", json!({"sessionId": "p"}));
        let out = graph(&g, key);
        assert_eq!(out.nodes[5], node(READ, FAILED, 2) | MERGE);
        assert_eq!(out.nodes[6], node(WEB, FAILED, 1) | FORK | MERGE);
        // the parent's next prompt clears it
        let gen = out.gen;
        assert!(cp(
            &mut g,
            "userPromptSubmitted",
            json!({"sessionId": "p", "prompt": "go"})
        ));
        let out = graph(&g, key);
        assert!(out.nodes.is_empty() && out.la.is_none());
        assert_ne!(out.gen, gen);
        // a sub-agent still working forks a fresh lane at its next call
        cp(
            &mut g,
            "preToolUse",
            cp_sub("a2", "code-review", "s5", "view"),
        );
        assert_eq!(graph(&g, key).nodes, vec![node(READ, RUNNING, 1) | FORK]);
    }

    #[test]
    fn claude_sub_agents_merge_on_subagent_stop() {
        let mut g = Graphs::default();
        let key = "claude:c";
        let pre = |id: &str, tool: &str, agent: Option<&str>| {
            let mut p = json!({"session_id": "c", "tool_use_id": id, "tool_name": tool,
                               "tool_input": {"command": "cargo test"}});
            if let Some(a) = agent {
                p["agent_id"] = json!(a);
                p["agent_type"] = json!("Explore");
            }
            p
        };
        cl(&mut g, "PreToolUse", pre("u1", "Task", None));
        cl(&mut g, "PreToolUse", pre("u2", "Bash", Some("x")));
        cl(&mut g, "PostToolUseFailure", pre("u2", "Bash", Some("x")));
        cl(&mut g, "PreToolUse", pre("u3", "AskUserQuestion", None));
        let stop = json!({"session_id": "c", "agent_id": "x", "agent_type": "Explore"});
        assert!(cl(&mut g, "SubagentStop", stop.clone()));
        assert!(!cl(&mut g, "SubagentStop", stop), "already merged");
        cl(&mut g, "PostToolUse", pre("u1", "Task", None));
        let out = graph(&g, key);
        assert_eq!(
            out.nodes,
            vec![
                node(AGENT, OK, 0),
                node(TEST, FAILED, 1) | FORK | MERGE,
                node(ASK, RUNNING, 0),
            ]
        );
        assert_eq!(out.la, Some(vec![0, 1]));
        // the session ends: its graph goes
        assert!(cl(&mut g, "SessionEnd", json!({"session_id": "c"})));
        assert!(graph(&g, key).nodes.is_empty());
        assert_eq!(graph(&g, key).gen, Some(0));
    }

    #[test]
    fn a_result_without_an_id_settles_the_newest_running_call_of_its_tool_in_its_lane() {
        let mut g = Graphs::default();
        let pre = json!({"sessionId": "p", "toolName": "bash"});
        cp(&mut g, "preToolUse", pre.clone());
        cp(&mut g, "preToolUse", pre.clone());
        cp(&mut g, "preToolUse", cp_sub("a", "", "s1", "bash"));
        let mut post = pre;
        post["toolResult"] = json!({"resultType": "denied"});
        assert!(cp(&mut g, "postToolUse", post));
        // a result nothing waits for changes nothing
        assert!(!cp(
            &mut g,
            "postToolUse",
            json!({"sessionId": "p", "toolName": "grep"})
        ));
        assert_eq!(
            graph(&g, "copilot:p").nodes,
            vec![
                node(SHELL, RUNNING, 0),
                node(SHELL, FAILED, 0),
                node(SHELL, RUNNING, 1) | FORK,
            ]
        );
    }

    #[test]
    fn the_parents_turn_end_fails_every_call_still_running() {
        for (agent, event, payload) in [
            ("claude", "Stop", json!({"session_id": "p"})),
            ("claude", "StopFailure", json!({"session_id": "p"})),
            ("copilot", "agentStop", json!({"sessionId": "p"})),
            ("copilot", "errorOccurred", json!({"sessionId": "p"})),
        ] {
            let mut g = Graphs::default();
            let key = format!("{agent}:p");
            let pre = |id: &str, tool: &str| {
                json!({"sessionId": "p", "session_id": "p", "toolCallId": id, "tool_use_id": id,
                       "toolName": tool, "tool_name": tool})
            };
            feed(&mut g, agent, "preToolUse", pre("1", "view"));
            feed(&mut g, agent, "postToolUse", pre("1", "view"));
            feed(&mut g, agent, "preToolUse", pre("2", "edit"));
            let mut sub = cp_sub("a", "explore", "s", "grep");
            sub["agent_id"] = json!("a");
            feed(&mut g, agent, "preToolUse", sub.clone());
            // a sub-agent's error is not the parent's turn ending
            assert!(!feed(&mut g, agent, "errorOccurred", sub), "{event}");
            let gen = graph(&g, &key).gen;
            assert!(feed(&mut g, agent, event, payload), "{event}");
            let out = graph(&g, &key);
            assert_eq!(
                out.nodes,
                vec![
                    node(READ, OK, 0),
                    node(EDIT, FAILED, 0),
                    node(SEARCH, FAILED, 1) | FORK | MERGE,
                ],
                "{event}"
            );
            assert_ne!(out.gen, gen, "{event}");
        }
    }

    #[test]
    fn eight_sub_agents_share_the_last_lane() {
        let mut g = Graphs::default();
        for i in 0..9 {
            let a = format!("a{i}");
            cp(
                &mut g,
                "preToolUse",
                cp_sub(&a, "task", &format!("s{i}"), "view"),
            );
        }
        let lanes: Vec<u64> = graph(&g, "copilot:p")
            .nodes
            .iter()
            .map(|n| n >> 6 & 7)
            .collect();
        assert_eq!(lanes, vec![1, 2, 3, 4, 5, 6, 7, 7, 7]);
        // a shared lane merges with its last sub-agent
        let stop = |a: &str| json!({"sessionId": a, "parentAgentId": "p", "agentId": a});
        cp(&mut g, "agentStop", stop("a8"));
        cp(&mut g, "agentStop", stop("a6"));
        assert!(graph(&g, "copilot:p").nodes.iter().all(|n| n & MERGE == 0));
        cp(&mut g, "agentStop", stop("a7"));
        let out = graph(&g, "copilot:p");
        assert_eq!(out.nodes[8], node(READ, RUNNING, 7) | FORK | MERGE);
        // lane 7 is free again
        cp(&mut g, "preToolUse", cp_sub("b", "plan", "s9", "view"));
        assert_eq!(
            *graph(&g, "copilot:p").nodes.last().unwrap(),
            node(READ, RUNNING, 7) | FORK
        );
        assert_eq!(graph(&g, "copilot:p").la.unwrap()[7], 6);
    }

    #[test]
    fn keeps_the_newest_64_calls_and_counts_the_rest() {
        let mut g = Graphs::default();
        for i in 0..70 {
            cp_call(&mut g, &format!("t{i}"), "glob", json!({}));
        }
        cp(
            &mut g,
            "preToolUse",
            json!({"sessionId": "p", "toolCallId": "x", "toolName": "edit"}),
        );
        let out = graph(&g, "copilot:p");
        assert_eq!(out.nodes.len(), MAX_NODES);
        assert_eq!(out.drop, Some(7));
        assert_eq!(*out.nodes.last().unwrap(), node(EDIT, RUNNING, 0));
        // results for dropped calls change nothing
        let gen = out.gen;
        assert!(!cp(
            &mut g,
            "postToolUse",
            json!({"sessionId": "p", "toolCallId": "t0", "toolName": "glob"})
        ));
        assert_eq!(graph(&g, "copilot:p").gen, gen);
    }

    #[test]
    fn sessions_are_bounded_and_remote_keys_are_their_own() {
        let mut g = Graphs::default();
        for i in 0..=MAX_SESSIONS {
            cp(
                &mut g,
                "preToolUse",
                json!({"sessionId": format!("s{i}"), "toolName": "view"}),
            );
        }
        assert_eq!(g.sessions.len(), MAX_SESSIONS);
        assert!(graph(&g, "copilot:s0").nodes.is_empty(), "the oldest went");
        assert_eq!(
            graph(&g, &format!("copilot:s{MAX_SESSIONS}")).nodes.len(),
            1
        );
        let remote = Via::Remote {
            host: "win".into(),
            os: "win".into(),
            attach: None,
        };
        let line = json!({"agent": "copilot", "event": "preToolUse",
                          "payload": {"sessionId": "s1", "toolName": "view"}});
        assert!(g.on_line(&line, &remote));
        assert_eq!(graph(&g, &remote.key("copilot:s1")).nodes.len(), 1);
        assert!(
            graph(&g, "copilot:s1").nodes.is_empty(),
            "evicted for the remote one"
        );
    }
}
