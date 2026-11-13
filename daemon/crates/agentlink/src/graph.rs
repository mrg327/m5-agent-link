//! `agentlink graph SID`: a session's agent graph (GRAPH.md) as the watch gets it, drawn as ASCII lanes.

use anyhow::Result;
use serde_json::{json, Value};

/// Kind letters, by kind code: other, read, edit, shell, search, web, agent, ask, plan, git, test, mcp.
const GLYPHS: &[u8] = b".RE$SWA?PGTM";
const LANE_KINDS: [&str; 8] = [
    "?",
    "explore",
    "task",
    "general-purpose",
    "code-review",
    "research",
    "plan",
    "other",
];

pub fn run(json: bool, sid: u64) -> Result<()> {
    let v = crate::status::ctl(&json!({"ctl": "graph", "sid": sid}))?;
    if let Some(e) = v["error"].as_str() {
        anyhow::bail!("session {sid}: {e}");
    }
    if json {
        println!("{v}");
    } else {
        print!("{}", render(&v));
    }
    Ok(())
}

/// Newest call at the top, one column per lane. A call shows its kind letter, `*` while it runs and `!`
/// when it failed; `|` is a lane that is open there.
fn render(g: &Value) -> String {
    let nodes: Vec<u64> = g["nodes"]
        .as_array()
        .map(|a| a.iter().filter_map(Value::as_u64).collect())
        .unwrap_or_default();
    let la = |l: usize| g["la"][l].as_u64().unwrap_or(0) as usize;
    let lane = |n: u64| (n >> 6 & 7) as usize;
    let top = nodes.iter().map(|n| lane(*n)).max().unwrap_or(0);
    let drop = g["drop"].as_u64().unwrap_or(0);
    // which lanes are open at each row, oldest first; a lane whose fork was dropped is open from the start
    let mut open = vec![false; top + 1];
    open[0] = true;
    let mut seen = vec![false; top + 1];
    for n in &nodes {
        let l = lane(*n);
        if l > 0 && !seen[l] {
            seen[l] = true;
            open[l] = drop > 0 && n >> 9 & 1 == 0;
        }
    }
    let mut rows = Vec::with_capacity(nodes.len());
    for n in &nodes {
        let l = lane(*n);
        if n >> 9 & 1 == 1 {
            open[l] = true;
        }
        let glyph = match n >> 4 & 3 {
            0 => '*',
            2 => '!',
            _ => GLYPHS.get((n & 15) as usize).map_or('.', |c| *c as char),
        };
        let mut row: String = (0..=top)
            .map(|c| match c {
                _ if c == l => glyph,
                _ if open[c] => '|',
                _ => ' ',
            })
            .flat_map(|c| [c, ' '])
            .collect();
        let row_len = row.trim_end().len();
        row.truncate(row_len);
        if n >> 9 & 1 == 1 {
            row.push_str(&format!("  <- fork {}", LANE_KINDS[la(l).min(7)]));
        }
        if n >> 10 & 1 == 1 {
            row.push_str("  -> merge");
            open[l] = false;
        }
        rows.push(row);
    }
    let mut out = format!(
        "session {} · gen {} · {} calls{}\n",
        g["sid"],
        g["gen"].as_u64().unwrap_or(0),
        nodes.len(),
        if drop > 0 {
            format!(" (+{drop} older)")
        } else {
            String::new()
        }
    );
    for r in rows.iter().rev() {
        out.push_str(r);
        out.push('\n');
    }
    if drop > 0 {
        out.push_str(":\n");
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn draws_lanes_newest_first() {
        // the protocol vector `graph`: read, agent, explore fork (shell) .. merge (search), failed edit,
        // running git
        let g = json!({"sid": 3, "nodes": [17, 22, 595, 1108, 34, 9], "la": [0, 1], "drop": 3, "gen": 42});
        assert_eq!(
            render(&g),
            "session 3 · gen 42 · 6 calls (+3 older)\n\
             *\n\
             !\n\
             | S  -> merge\n\
             | $  <- fork explore\n\
             A\n\
             R\n\
             :\n"
        );
    }
}
