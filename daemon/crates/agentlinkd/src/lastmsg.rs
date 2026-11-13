//! The agent's last message as the watch reads it (LAST-MESSAGE.md §2.2): markdown turned into plain
//! text, symbols the watch cannot draw given a stand-in or dropped, capped at the reader's buffer.
//! Pure text work.

use crate::broker::DETAIL_CAP;

/// Longest start of a message shown on the card (`session.lp`), in bytes.
pub const PREVIEW_MAX: usize = 160;
/// Lines of a fenced code block kept before the rest is counted.
const CODE_LINES: usize = 6;
/// How far back from the cap a line break is looked for, so the cut ends a line.
const CUT_SLACK: usize = 400;

/// `text` as plain, drawable text of at most [`DETAIL_CAP`] bytes; "" when nothing readable is left.
pub fn clean(text: &str) -> String {
    let text = text.replace("\r\n", "\n").replace('\r', "\n");
    let lines: Vec<&str> = text.split('\n').collect();
    let mut out: Vec<String> = Vec::new();
    let mut fence: Option<(String, Vec<String>)> = None;
    // indents of the open list levels, outermost first
    let mut list: Vec<usize> = Vec::new();
    let mut in_table = false;
    for (i, raw) in lines.iter().enumerate() {
        if let Some((marker, body)) = &mut fence {
            if raw.trim_start().starts_with(marker.as_str()) && raw.trim().len() >= marker.len() {
                flush_code(&mut out, body);
                fence = None;
            } else {
                body.push(raw.trim_end().to_owned());
            }
            continue;
        }
        let (quoted, line) = strip_quote(raw);
        let trimmed = line.trim();
        if let Some(marker) = fence_marker(trimmed) {
            fence = Some((marker, Vec::new()));
            continue;
        }
        if trimmed.is_empty() {
            out.push(String::new());
            in_table = false;
            continue;
        }
        let next = lines.get(i + 1).map(|l| strip_quote(l).1.trim());
        if is_table_separator(trimmed) {
            in_table = true;
            continue;
        }
        if trimmed.contains('|')
            && (trimmed.starts_with('|') || in_table || next.is_some_and(is_table_separator))
        {
            in_table = true;
            out.push(table_row(trimmed));
            continue;
        }
        in_table = false;
        if is_rule(trimmed) {
            out.push(String::new());
            list.clear();
            continue;
        }
        if let Some(h) = heading(trimmed) {
            out.push(inline(h));
            list.clear();
            continue;
        }
        let indent = indent_of(line);
        if let Some((marker, rest)) = list_item(line.trim_start()) {
            while list.last().is_some_and(|&top| top > indent) {
                list.pop();
            }
            if list.last() != Some(&indent) {
                list.push(indent);
            }
            let pad = "  ".repeat(list.len() - 1);
            out.push(format!("{pad}{marker}{}", inline(rest)));
            continue;
        }
        // a continuation line of a list item keeps the item's indent; anything else ends the list
        let pad = if indent > 0 && !list.is_empty() && !quoted {
            "  ".repeat(list.len())
        } else {
            list.clear();
            String::new()
        };
        out.push(format!("{pad}{}", inline(trimmed)));
    }
    if let Some((_, body)) = &mut fence {
        flush_code(&mut out, body); // an unclosed fence runs to the end
    }
    cap(&blank_runs(&drawable(&out.join("\n"))))
}

/// The start of a cleaned message for the card: its first paragraph (after a heading-like line, the
/// paragraph under it too), on one line, at most [`PREVIEW_MAX`] bytes, cut between words.
pub fn preview(cleaned: &str) -> String {
    let mut words: Vec<&str> = Vec::new();
    for para in cleaned.split("\n\n") {
        words.extend(para.split_whitespace());
        let heading = !para.contains('\n') && para.len() < 60 && !para.ends_with(['.', '!', '?']);
        if !words.is_empty() && !heading {
            break;
        }
    }
    let line = words.join(" ");
    if line.len() <= PREVIEW_MAX {
        return line;
    }
    let room = PREVIEW_MAX - '…'.len_utf8();
    let head = &line[..floor_boundary(&line, room)];
    let cut = match head.rfind(' ') {
        Some(sp) if sp > room / 2 => head[..sp].trim_end(),
        _ => head,
    };
    format!("{cut}…")
}

/// `(true, rest)` for a blockquote line (`> text`, nested `>>` too), else `(false, line)`.
fn strip_quote(line: &str) -> (bool, &str) {
    let mut rest = line.trim_start();
    if !rest.starts_with('>') {
        return (false, line);
    }
    while let Some(r) = rest.strip_prefix('>') {
        rest = r
            .strip_prefix(' ')
            .unwrap_or(r)
            .trim_start_matches(['>', ' ']);
    }
    (true, rest)
}

/// The fence a code block opens with (three or more backticks or tildes), if this line opens one.
fn fence_marker(trimmed: &str) -> Option<String> {
    let c = trimmed.chars().next().filter(|c| *c == '`' || *c == '~')?;
    let n = trimmed.chars().take_while(|x| *x == c).count();
    (n >= 3).then(|| c.to_string().repeat(n))
}

fn flush_code(out: &mut Vec<String>, body: &mut Vec<String>) {
    while body.last().is_some_and(String::is_empty) {
        body.pop();
    }
    let more = body.len().saturating_sub(CODE_LINES);
    out.extend(body.drain(..).take(CODE_LINES));
    if more > 0 {
        out.push(format!(
            "  … {more} more line{}",
            if more == 1 { "" } else { "s" }
        ));
    }
}

/// `|---|:--:|` or `--- | ---`: a table's header separator.
fn is_table_separator(trimmed: &str) -> bool {
    trimmed.contains('-')
        && trimmed.contains('|')
        && trimmed.chars().all(|c| matches!(c, '|' | '-' | ':' | ' '))
}

/// `| a | b |` as `a · b`.
fn table_row(trimmed: &str) -> String {
    let inner = trimmed.strip_prefix('|').unwrap_or(trimmed);
    let inner = inner.strip_suffix('|').unwrap_or(inner);
    inner
        .split('|')
        .map(|c| inline(c.trim()))
        .collect::<Vec<_>>()
        .join(" · ")
}

/// `---`, `***`, `___` (spaces allowed between): a horizontal rule.
fn is_rule(trimmed: &str) -> bool {
    let c = trimmed.chars().next().unwrap_or(' ');
    matches!(c, '-' | '*' | '_')
        && trimmed.chars().filter(|x| *x == c).count() >= 3
        && trimmed.chars().all(|x| x == c || x == ' ')
}

/// The text of an ATX heading (`## Title ##`).
fn heading(trimmed: &str) -> Option<&str> {
    let n = trimmed.chars().take_while(|c| *c == '#').count();
    if n == 0 || n > 6 {
        return None;
    }
    let rest = &trimmed[n..];
    if !rest.is_empty() && !rest.starts_with(' ') {
        return None; // "#hashtag", "#123"
    }
    Some(rest.trim().trim_end_matches('#').trim_end())
}

/// Leading whitespace in columns, a tab counting four.
fn indent_of(line: &str) -> usize {
    line.chars()
        .take_while(|c| c.is_whitespace())
        .map(|c| if c == '\t' { 4 } else { 1 })
        .sum()
}

/// A list item: (`"• "` or the item's own number, the rest).
fn list_item(s: &str) -> Option<(String, &str)> {
    for b in ["- ", "* ", "+ "] {
        if let Some(rest) = s.strip_prefix(b) {
            return Some(("• ".into(), rest.trim_start()));
        }
    }
    let digits = s.chars().take_while(char::is_ascii_digit).count();
    if (1..=9).contains(&digits) {
        let after = &s[digits..];
        for d in [". ", ") "] {
            if let Some(rest) = after.strip_prefix(d) {
                return Some((format!("{}{d}", &s[..digits]), rest.trim_start()));
            }
        }
    }
    None
}

/// Inline markdown removed: code spans keep their text verbatim, links and images become their text,
/// emphasis (`**`, `__`, `*`, `_`, `~~`) loses its markers where it is clearly emphasis.
fn inline(s: &str) -> String {
    // (char, protected): code span content and escaped characters are never markup
    let chars: Vec<char> = s.chars().collect();
    let mut v: Vec<(char, bool)> = Vec::with_capacity(chars.len());
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c == '\\' && chars.get(i + 1).is_some_and(|n| n.is_ascii_punctuation()) {
            v.push((chars[i + 1], true));
            i += 2;
            continue;
        }
        if c == '`' {
            let n = run(&chars, i, '`');
            if let Some(end) = find_run(&chars, i + n, '`', n) {
                let body: String = chars[i + n..end].iter().collect();
                let body = if body.len() > 2 && body.starts_with(' ') && body.ends_with(' ') {
                    &body[1..body.len() - 1]
                } else {
                    &body[..]
                };
                v.extend(body.chars().map(|c| (c, true)));
                i = end + n;
                continue;
            }
            v.extend(std::iter::repeat_n(('`', true), n));
            i += n;
            continue;
        }
        v.push((c, false));
        i += 1;
    }
    let v = links(v);
    emphasis(&v)
}

/// Length of the run of `c` starting at `i`.
fn run(chars: &[char], i: usize, c: char) -> usize {
    chars[i..].iter().take_while(|x| **x == c).count()
}

/// Start of the next run of exactly `n` `c`s from `from`.
fn find_run(chars: &[char], from: usize, c: char, n: usize) -> Option<usize> {
    let mut j = from;
    while j < chars.len() {
        if chars[j] == c {
            let m = run(chars, j, c);
            if m == n {
                return Some(j);
            }
            j += m;
        } else {
            j += 1;
        }
    }
    None
}

/// `[text](url)`, `![alt](url)` and `[text][ref]` become their text; `<https://…>` its address.
fn links(v: Vec<(char, bool)>) -> Vec<(char, bool)> {
    let free = |k: usize, c: char| v.get(k).is_some_and(|x| *x == (c, false));
    let mut out = Vec::with_capacity(v.len());
    let mut i = 0;
    while i < v.len() {
        let image = free(i, '!') && free(i + 1, '[');
        let open = if image { i + 1 } else { i };
        if free(open, '[') {
            // the matching `]`
            let (mut depth, mut j) = (0, open);
            let close = loop {
                match v.get(j) {
                    None => break None,
                    Some(('[', false)) => depth += 1,
                    Some((']', false)) => {
                        depth -= 1;
                        if depth == 0 {
                            break Some(j);
                        }
                    }
                    _ => {}
                }
                j += 1;
            };
            if let Some(close) = close {
                let tail = if free(close + 1, '(') {
                    (close + 1..v.len()).find(|&k| free(k, ')'))
                } else if free(close + 1, '[') {
                    (close + 1..v.len()).find(|&k| free(k, ']'))
                } else {
                    None
                };
                if let Some(end) = tail {
                    out.extend(links(v[open + 1..close].to_vec()));
                    i = end + 1;
                    continue;
                }
            }
        }
        if free(i, '<') {
            if let Some(end) = (i + 1..v.len()).find(|&k| free(k, '>')) {
                let inner: String = v[i + 1..end].iter().map(|x| x.0).collect();
                if inner.starts_with("http://") || inner.starts_with("https://") {
                    out.extend(inner.chars().map(|c| (c, true)));
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push(v[i]);
        i += 1;
    }
    out
}

/// Removes emphasis markers. A run of `*`, `_` (1-3) or `~~` opens where it is followed by a non-space
/// and not preceded by a letter or digit, and closes at the next run of the same kind and length that is
/// preceded by a non-space and not followed by a letter or digit. So `snake_case`, `2 * 3` and `a*b`
/// stay as they are; a dunder name (`__init__`) is not taken for bold.
fn emphasis(v: &[(char, bool)]) -> String {
    let n = v.len();
    let mut gone = vec![false; n];
    let markup = |k: usize, c: char| v.get(k).is_some_and(|x| x.0 == c && !x.1);
    let word = |k: Option<usize>| {
        k.and_then(|k| v.get(k))
            .is_some_and(|x| x.0.is_alphanumeric())
    };
    let space = |k: Option<usize>| k.and_then(|k| v.get(k)).is_none_or(|x| x.0.is_whitespace());
    let mut i = 0;
    while i < n {
        let c = v[i].0;
        if v[i].1 || gone[i] || !matches!(c, '*' | '_' | '~') {
            i += 1;
            continue;
        }
        let mut len = 0;
        while markup(i + len, c) {
            len += 1;
        }
        let ok_len = if c == '~' { len == 2 } else { len <= 3 };
        let opens = ok_len && !word(i.checked_sub(1)) && !space(Some(i + len));
        if !opens {
            i += len;
            continue;
        }
        // the closer
        let mut j = i + len;
        let mut close = None;
        while j < n {
            if markup(j, c) && !gone[j] {
                let mut m = 0;
                while markup(j + m, c) {
                    m += 1;
                }
                if m == len && !space(j.checked_sub(1)) && !word(Some(j + m)) {
                    close = Some(j);
                    break;
                }
                j += m;
            } else {
                j += 1;
            }
        }
        let Some(j) = close else {
            i += len;
            continue;
        };
        let inner: String = v[i + len..j].iter().map(|x| x.0).collect();
        let dunder = c == '_' && len == 2 && inner.chars().all(|x| x.is_alphanumeric() || x == '_');
        if dunder {
            i = j + len;
            continue;
        }
        for k in (i..i + len).chain(j..j + len) {
            gone[k] = true;
        }
        i += len;
    }
    v.iter()
        .zip(gone)
        .filter(|(_, g)| !g)
        .map(|(x, _)| x.0)
        .collect()
}

/// Only what the watch can draw: a few common symbols get a stand-in, other characters outside its
/// glyph set (emoji, variation selectors, joiners) are dropped, with the space they leave behind.
fn drawable(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut dropped = false;
    for c in s.chars() {
        let stand_in = match c {
            '\n' => Some("\n"),
            '\t' => Some("    "),
            '\u{a0}' | '\u{2002}'..='\u{200a}' | '\u{202f}' => Some(" "),
            '\u{2192}' | '\u{27f6}' | '\u{279c}' | '\u{2794}' => Some("->"),
            '\u{2190}' | '\u{27f5}' => Some("<-"),
            '\u{2194}' => Some("<->"),
            '\u{21d2}' | '\u{27f9}' => Some("=>"),
            '\u{2265}' => Some(">="),
            '\u{2264}' => Some("<="),
            '\u{2260}' => Some("!="),
            '\u{2248}' => Some("~"),
            '\u{2212}' | '\u{2010}' | '\u{2011}' => Some("-"),
            '\u{2032}' => Some("'"),
            '\u{2033}' => Some("\""),
            '\u{2713}' | '\u{2714}' | '\u{2705}' | '\u{2611}' => Some("[x]"),
            '\u{274c}' | '\u{2717}' | '\u{2718}' | '\u{274e}' => Some("[ ]"),
            '\u{25e6}' | '\u{2023}' | '\u{25aa}' | '\u{25b8}' | '\u{2219}' => Some("•"),
            _ => None,
        };
        if let Some(t) = stand_in {
            out.push_str(t);
            dropped = false;
            continue;
        }
        // the watch's glyph set, as `broker::detail_text` reads it
        let keep = matches!(c,
            '\u{20}'..='\u{7e}' | '\u{a1}'..='\u{ac}' | '\u{ae}'..='\u{ff}'
            | '\u{2013}' | '\u{2014}' | '\u{2018}' | '\u{2019}' | '\u{201c}' | '\u{201d}'
            | '\u{2022}' | '\u{2026}');
        if keep {
            // "✨ Done" reads "Done", not " Done"
            if !(dropped && c == ' ' && (out.is_empty() || out.ends_with([' ', '\n']))) {
                out.push(c);
            }
            dropped = false;
        } else {
            dropped = true;
        }
    }
    out
}

/// Trailing spaces gone, runs of blank lines down to one, no blank lines at either end.
fn blank_runs(s: &str) -> String {
    let mut out: Vec<&str> = Vec::new();
    for line in s.split('\n').map(str::trim_end) {
        if line.is_empty() && out.last().is_none_or(|l| l.is_empty()) {
            continue;
        }
        out.push(line);
    }
    while out.last().is_some_and(|l| l.is_empty()) {
        out.pop();
    }
    out.join("\n")
}

/// At most [`DETAIL_CAP`] bytes: cut at the end of a line near the cap when there is one, else between
/// characters, and marked with `…`.
fn cap(s: &str) -> String {
    if s.len() <= DETAIL_CAP {
        return s.to_owned();
    }
    let room = DETAIL_CAP - "\n…".len();
    let head = &s[..floor_boundary(s, room)];
    if let Some(nl) = head.rfind('\n').filter(|&nl| nl + CUT_SLACK >= head.len()) {
        let kept = head[..nl].trim_end();
        if !kept.is_empty() {
            return format!("{kept}\n…");
        }
    }
    let head = &s[..floor_boundary(s, DETAIL_CAP - '…'.len_utf8())];
    format!("{head}…")
}

/// The largest character boundary of `s` at or below `at`.
fn floor_boundary(s: &str, at: usize) -> usize {
    let mut at = at.min(s.len());
    while !s.is_char_boundary(at) {
        at -= 1;
    }
    at
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn headings_emphasis_and_code_spans_are_plain_words() {
        assert_eq!(
            clean("## Summary ##\n\nThe **build** is __all green__ and *fast*; see `cargo test`."),
            "Summary\n\nThe build is all green and fast; see cargo test."
        );
        assert_eq!(
            clean("***both*** and ~~gone~~ and _it_"),
            "both and gone and it"
        );
        // markup inside a code span is the code's own
        assert_eq!(clean("run `a **b** c`"), "run a **b** c");
        assert_eq!(clean("``a ` b``"), "a ` b");
        assert_eq!(clean(r"literal \*stars\*"), "literal *stars*");
        assert_eq!(clean("#hashtag stays"), "#hashtag stays");
    }

    #[test]
    fn identifiers_and_arithmetic_are_not_taken_for_emphasis() {
        for s in [
            "set my_var_name and other_var",
            "call __init__ first",
            "2 * 3 * 4 = 24",
            "a*b*c",
            "glob src/**/*.rs",
            "file_a.rs and file_b.rs",
        ] {
            assert_eq!(clean(s), s, "{s}");
        }
        assert_eq!(clean("**`snake_case`** name"), "snake_case name");
    }

    #[test]
    fn bullets_nest_by_two_spaces_and_numbers_stay() {
        let md = "Done:\n- one\n  - nested *a*\n    * deeper\n- two\n+ three\n\n1. first\n2. second\n   - under";
        assert_eq!(
            clean(md),
            "Done:\n• one\n  • nested a\n    • deeper\n• two\n• three\n\n1. first\n2. second\n  • under"
        );
        // four-space nesting reads the same
        assert_eq!(clean("- a\n    - b"), "• a\n  • b");
        // a continuation line of an item keeps its indent
        assert_eq!(clean("- item\n  more of it"), "• item\n  more of it");
    }

    #[test]
    fn quotes_rules_and_blank_runs() {
        assert_eq!(
            clean("> quoted **text**\n> - in a list\n\n---\n\nafter"),
            "quoted text\n• in a list\n\nafter"
        );
        assert_eq!(clean("a\n\n\n\n\nb\n\n\n"), "a\n\nb");
        assert_eq!(clean("a\r\nb\rc"), "a\nb\nc");
        assert_eq!(clean("x\n* * *\ny"), "x\n\ny");
    }

    #[test]
    fn a_code_block_keeps_six_lines_and_counts_the_rest() {
        let body: Vec<String> = (1..=10).map(|i| format!("  line {i}")).collect();
        let md = format!("Run:\n\n```rust\n{}\n```\n\nok", body.join("\n"));
        let want = format!("Run:\n\n{}\n  … 4 more lines\n\nok", body[..6].join("\n"));
        assert_eq!(clean(&md), want);
        // markup inside the block is the code's own, indentation is kept
        assert_eq!(
            clean("~~~\n# not a heading\n    **x**\n~~~"),
            "# not a heading\n    **x**"
        );
        let seven = format!("```\n{}\n```", ["x"; 7].join("\n"));
        assert!(clean(&seven).ends_with("  … 1 more line"));
        // an unclosed fence runs to the end
        assert_eq!(clean("```\na\nb"), "a\nb");
    }

    #[test]
    fn a_table_is_one_line_per_row() {
        let md = "| Test | Result |\n|---|:---:|\n| unit | **pass** |\n| e2e | fail |\n\nafter";
        assert_eq!(clean(md), "Test · Result\nunit · pass\ne2e · fail\n\nafter");
        assert_eq!(clean("a | b\n--- | ---\n1 | 2"), "a · b\n1 · 2");
        // a pipe in prose is not a table
        assert_eq!(clean("use a | b to pipe"), "use a | b to pipe");
    }

    #[test]
    fn links_and_images_become_their_text() {
        assert_eq!(
            clean("See [the docs](https://example.com/a_b) and ![a chart](c.png), [ref][1], <https://x.org>."),
            "See the docs and a chart, ref, https://x.org."
        );
        assert_eq!(clean("[not a link] stays"), "[not a link] stays");
        assert_eq!(clean("[**bold** link](u)"), "bold link");
    }

    #[test]
    fn symbols_get_stand_ins_and_emoji_are_dropped() {
        assert_eq!(clean("a → b ← c ≥ 1 ≤ 2"), "a -> b <- c >= 1 <= 2");
        assert_eq!(
            clean("✅ tests\n❌ lint\n✓ ok"),
            "[x] tests\n[ ] lint\n[x] ok"
        );
        assert_eq!(clean("🎉 Done! ✨ Shipped 🚀"), "Done! Shipped");
        assert_eq!(clean("❤\u{fe0f} ok"), "ok");
        assert_eq!(clean("👩\u{200d}💻 coder"), "coder");
        assert_eq!(
            clean("a\u{a0}b 3×4 café — “q” … •"),
            "a b 3×4 café — “q” … •"
        );
        assert_eq!(clean("tab\there"), "tab    here");
        assert_eq!(clean("bell\u{7}"), "bell");
        assert_eq!(clean("🎉"), "", "nothing readable left");
    }

    #[test]
    fn the_cap_is_utf8_safe_and_prefers_a_line_end() {
        let long = (0..400)
            .map(|i| format!("line {i} with é and —"))
            .collect::<Vec<_>>()
            .join("\n");
        let c = clean(&long);
        assert!(c.len() <= DETAIL_CAP, "{} bytes", c.len());
        assert!(c.ends_with("\n…"));
        assert!(long.starts_with(c.trim_end_matches("\n…")));
        // one long line: cut between characters
        let wide = "é—".repeat(2000);
        let c = clean(&wide);
        assert!(c.len() <= DETAIL_CAP && c.len() > DETAIL_CAP - 8);
        assert!(c.ends_with('…'));
        assert!(wide.starts_with(c.trim_end_matches('…')));
        // just under the cap: untouched
        let fits = "x".repeat(DETAIL_CAP);
        assert_eq!(clean(&fits), fits);
    }

    #[test]
    fn the_preview_is_the_first_paragraph_on_one_line() {
        assert_eq!(
            preview("Built and flashed.\nAll tests pass.\n\nNext: the docs."),
            "Built and flashed. All tests pass."
        );
        // a short heading is followed by its text
        assert_eq!(
            preview("Summary\n\nThe change is in.\n\nMore."),
            "Summary The change is in."
        );
        let long = "word ".repeat(60);
        let p = preview(&long);
        assert!(p.len() <= PREVIEW_MAX && p.ends_with("word…"), "{p}");
        let unbroken = "é".repeat(200);
        let p = preview(&unbroken);
        assert!(p.len() <= PREVIEW_MAX && p.ends_with('…'));
        assert_eq!(preview(""), "");
    }
}
