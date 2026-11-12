//! Standing rules from the watch (docs/design/STANDING-RULES.md): "always allow this" on a permission
//! request, granted with a hold.
//!
//! Claude Code's `PermissionRequest` hook input carries `permission_suggestions`: the permission
//! updates its own dialog would apply for "Yes, and don't ask again" (hooks reference, "Permission
//! update entries"). The watch may grant **one of them, verbatim**, and only one it can show in full:
//!
//! - `type` `addRules`, `behavior` `allow`, and no key besides `type`, `rules`, `behavior`,
//!   `destination` (an entry this build does not fully understand is not offered);
//! - `destination` `session` (this session only) or `localSettings` (this project, the user's own
//!   `.claude/settings.local.json`). `projectSettings` (a file usually checked in and shared) and
//!   `userSettings` (every project) are never offered from the watch;
//! - one to [`MAX_RULES`] rules, each `{toolName, ruleContent}` and nothing else. A rule without
//!   `ruleContent` matches the whole tool and is never offered; nor is a wildcard anywhere but at the
//!   end (`git * main`, `* --version`), a content that is only a wildcard, or a character outside
//!   printable ASCII (the watch must be able to show every character as it is);
//! - all its rules, one per line as Claude Code writes them (`Bash(npm test:*)`), within [`RULE_CAP`]
//!   bytes.
//!
//! `setMode` (for example `acceptEdits`, which stops every edit prompt for the session),
//! `addDirectories`, `replaceRules`, `removeRules` and deny or ask rules are never offered. Of the
//! entries that pass, the narrowest destination wins (session before project); the first among equals.
//! The daemon never builds a rule of its own: the entry it hands back is the one Claude Code sent.

use agentlink_proto::RuleScope;
use serde_json::Value;

/// Longest rule text (all rules, with their line breaks) the watch is offered. The watch's model keeps
/// one such text per request (`attention_t.rule`), and two lines of its platter show about 60 characters.
pub const RULE_CAP: usize = 160;
/// Claude Code saves up to five rules for one compound command (permissions, "Compound commands").
pub const MAX_RULES: usize = 5;

/// The standing rule a request offers.
#[derive(Debug, Clone, PartialEq)]
pub struct Standing {
    /// The suggestion entry, exactly as the hook received it: what the hook will hand back.
    pub entry: Value,
    /// Its rules as Claude Code saves them, one per line: what the watch shows.
    pub text: String,
    pub scope: RuleScope,
    /// A rule ends in a wildcard: it matches more than this exact request.
    pub wide: bool,
    /// [`hash`] of what the watch shows; the device's grant must name it.
    pub hash: u32,
}

/// Claude Code's `destination` for a scope the watch may grant.
pub fn scope_of(destination: &str) -> Option<RuleScope> {
    match destination {
        "session" => Some(RuleScope::Session),
        "localSettings" => Some(RuleScope::Project),
        _ => None,
    }
}

/// The scope's wire value (protocol `rule_scope`), also its rank: smaller is narrower.
pub fn scope_code(s: RuleScope) -> u8 {
    match s {
        RuleScope::Session => 0,
        RuleScope::Project => 1,
    }
}

/// The scope in the words the audit log and the watch use.
pub fn scope_words(s: RuleScope) -> &'static str {
    match s {
        RuleScope::Session => "this session",
        RuleScope::Project => "this project",
    }
}

/// FNV-1a (32 bit) over the rule text, a 0 byte, the scope's wire value and the wide flag: binds a grant
/// to the rule, the scope and the breadth the device showed. `rule_hash` in the firmware's model.c
/// computes the same; protocol vector `cmd_approve_rule` pins one value.
pub fn hash(text: &str, scope: RuleScope, wide: bool) -> u32 {
    let mut h: u32 = 0x811c_9dc5;
    for b in text.bytes().chain([0, scope_code(scope), u8::from(wide)]) {
        h ^= u32::from(b);
        h = h.wrapping_mul(0x0100_0193);
    }
    h
}

/// Whether a rule's content may be shown and granted from the watch, and if so whether it is wide.
fn breadth(c: &str) -> Option<bool> {
    if c.is_empty() || !c.bytes().all(|b| (0x20..0x7f).contains(&b)) {
        return None;
    }
    let (lit, wide) = match c
        .strip_suffix(":*")
        .or_else(|| c.strip_suffix(" *"))
        .or_else(|| c.strip_suffix("/**"))
    {
        Some(l) => (l, true),
        None => (c, false),
    };
    // a space at either end is invisible on the watch; a `*` left in the literal is a wildcard inside it
    if lit.trim().is_empty() || lit.contains('*') || lit != lit.trim() {
        return None;
    }
    Some(wide)
}

/// One rule, as Claude Code writes it, and whether it is wide.
fn rule_text(rule: &Value) -> Option<(String, bool)> {
    let o = rule.as_object()?;
    if o.keys().any(|k| k != "toolName" && k != "ruleContent") {
        return None;
    }
    let tool = o.get("toolName")?.as_str()?;
    if tool.is_empty()
        || !tool
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
    {
        return None;
    }
    // no ruleContent: the whole tool. Never from the watch.
    let content = o.get("ruleContent")?.as_str()?;
    let wide = breadth(content)?;
    Some((format!("{tool}({content})"), wide))
}

/// The entry as a standing rule the watch may offer, or `None`.
pub fn accept(e: &Value) -> Option<Standing> {
    let o = e.as_object()?;
    if o.len() != 4
        || o.get("type")?.as_str()? != "addRules"
        || o.get("behavior")?.as_str()? != "allow"
    {
        return None;
    }
    let scope = scope_of(o.get("destination")?.as_str()?)?;
    let rules = o.get("rules")?.as_array()?;
    if rules.is_empty() || rules.len() > MAX_RULES {
        return None;
    }
    let mut lines = Vec::with_capacity(rules.len());
    let mut wide = false;
    for r in rules {
        let (t, w) = rule_text(r)?;
        wide |= w;
        lines.push(t);
    }
    let text = lines.join("\n");
    if text.len() > RULE_CAP {
        return None;
    }
    Some(Standing {
        entry: e.clone(),
        hash: hash(&text, scope, wide),
        text,
        scope,
        wide,
    })
}

/// The standing rule a `PermissionRequest` payload offers the watch: the narrowest acceptable suggestion.
pub fn pick(payload: &Value) -> Option<Standing> {
    let mut best: Option<Standing> = None;
    for e in payload["permission_suggestions"].as_array()? {
        if let Some(s) = accept(e) {
            if best
                .as_ref()
                .is_none_or(|b| scope_code(s.scope) < scope_code(b.scope))
            {
                best = Some(s);
            }
        }
    }
    best
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn add(rules: Value, dest: &str) -> Value {
        json!({"type": "addRules", "rules": rules, "behavior": "allow", "destination": dest})
    }

    fn one(tool: &str, content: &str) -> Value {
        json!([{"toolName": tool, "ruleContent": content}])
    }

    #[test]
    fn the_docs_example_is_offered_verbatim() {
        let e = add(one("Bash", "rm -rf node_modules"), "localSettings");
        let s = accept(&e).unwrap();
        assert_eq!(s.text, "Bash(rm -rf node_modules)");
        assert_eq!((s.scope, s.wide), (RuleScope::Project, false));
        assert_eq!(s.entry, e);
    }

    #[test]
    fn trailing_wildcards_are_wide() {
        for c in ["npm test:*", "npm test *"] {
            let s = accept(&add(one("Bash", c), "session")).unwrap();
            assert!(s.wide, "{c}");
            assert_eq!(s.text, format!("Bash({c})"));
        }
        let s = accept(&add(one("Read", "//srv/data/**"), "session")).unwrap();
        assert!(s.wide);
        let s = accept(&add(one("WebFetch", "domain:example.com"), "localSettings")).unwrap();
        assert!(!s.wide);
    }

    #[test]
    fn whole_tool_and_inner_wildcards_are_never_offered() {
        for rules in [
            json!([{"toolName": "Bash"}]),
            one("Bash", "*"),
            one("Bash", ":*"),
            one("Bash", " *"),
            one("Bash", "git * main"),
            one("Bash", "* --version"),
            one("Bash", "npm*"),
            one("Bash", ""),
            one("WebFetch", "domain:*.example.com"),
            json!([{"toolName": "mcp__github__*"}]),
        ] {
            assert!(accept(&add(rules.clone(), "session")).is_none(), "{rules}");
        }
    }

    #[test]
    fn text_the_watch_could_misread_is_never_offered() {
        for c in [
            "echo a\nrm -rf /",
            "echo\tx",
            "echo \u{a0}x",
            "caf\u{e9}",
            " npm test",
            "npm test ",
            "\u{1b}[2Jls",
        ] {
            assert!(accept(&add(one("Bash", c), "session")).is_none(), "{c:?}");
        }
        assert!(accept(&add(one("Ba sh", "ls"), "session")).is_none());
        let long = "x".repeat(RULE_CAP);
        assert!(accept(&add(one("Bash", &long), "session")).is_none());
    }

    #[test]
    fn only_allow_rules_to_the_two_narrow_destinations() {
        assert!(accept(&add(one("Bash", "ls"), "projectSettings")).is_none());
        assert!(accept(&add(one("Bash", "ls"), "userSettings")).is_none());
        assert!(accept(&add(one("Bash", "ls"), "cliArg")).is_none());
        let mut deny = add(one("Bash", "ls"), "session");
        deny["behavior"] = json!("deny");
        assert!(accept(&deny).is_none());
        for e in [
            json!({"type": "setMode", "mode": "acceptEdits", "destination": "session"}),
            json!({"type": "addDirectories", "directories": ["/tmp"], "destination": "session"}),
            json!({"type": "replaceRules", "rules": one("Bash", "ls"), "behavior": "allow", "destination": "session"}),
            json!({"type": "removeRules", "rules": one("Bash", "ls"), "behavior": "allow", "destination": "session"}),
        ] {
            assert!(accept(&e).is_none(), "{e}");
        }
        // a key this build does not know: not offered, rather than passed back half understood
        let mut extra = add(one("Bash", "ls"), "session");
        extra["mode"] = json!("bypassPermissions");
        assert!(accept(&extra).is_none());
        let rule_extra = add(
            json!([{"toolName": "Bash", "ruleContent": "ls", "x": 1}]),
            "session",
        );
        assert!(accept(&rule_extra).is_none());
    }

    #[test]
    fn several_rules_show_one_per_line_and_all_must_pass() {
        let e = add(
            json!([{"toolName": "Bash", "ruleContent": "npm test:*"}, {"toolName": "Bash", "ruleContent": "make lint"}]),
            "localSettings",
        );
        let s = accept(&e).unwrap();
        assert_eq!(s.text, "Bash(npm test:*)\nBash(make lint)");
        assert!(s.wide);
        let bad = add(
            json!([{"toolName": "Bash", "ruleContent": "make lint"}, {"toolName": "Bash"}]),
            "localSettings",
        );
        assert!(accept(&bad).is_none());
        let six: Vec<Value> = (0..6)
            .map(|i| json!({"toolName": "Bash", "ruleContent": format!("t{i}")}))
            .collect();
        assert!(accept(&add(Value::Array(six), "session")).is_none());
        assert!(accept(&add(json!([]), "session")).is_none());
    }

    #[test]
    fn the_narrowest_acceptable_suggestion_wins() {
        let p = json!({"permission_suggestions": [
            {"type": "setMode", "mode": "acceptEdits", "destination": "session"},
            add(one("Bash", "npm test:*"), "localSettings"),
            add(one("Bash", "npm test:*"), "session"),
            add(one("Bash", "npm test:*"), "userSettings"),
        ]});
        let s = pick(&p).unwrap();
        assert_eq!(s.scope, RuleScope::Session);
        assert_eq!(s.entry, p["permission_suggestions"][2]);
        let only_mode = json!({"permission_suggestions": [{"type": "setMode", "mode": "acceptEdits", "destination": "session"}]});
        assert!(pick(&only_mode).is_none());
        assert!(pick(&json!({})).is_none());
        assert!(pick(&json!({"permission_suggestions": "x"})).is_none());
    }

    #[test]
    fn the_hash_binds_text_scope_and_breadth() {
        // FNV-1a 32; the firmware's rule_hash must agree (protocol vector cmd_approve_rule pins 0x516a52ed)
        let base = hash("Bash(npm test:*)", RuleScope::Project, true);
        assert_eq!(base, 0x516a_52ed);
        assert_eq!(base, hash("Bash(npm test:*)", RuleScope::Project, true));
        assert_ne!(base, hash("Bash(npm test:*)", RuleScope::Session, true));
        assert_ne!(base, hash("Bash(npm test:*)", RuleScope::Project, false));
        assert_ne!(base, hash("Bash(npm tests:*)", RuleScope::Project, true));
        assert_eq!(hash("", RuleScope::Session, false), 0x4ab0_f7b7);
    }
}
