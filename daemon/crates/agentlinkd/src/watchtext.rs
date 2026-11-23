//! Text as the watch can show it. Its fonts draw ASCII, Latin-1 and a few punctuation marks
//! (`firmware/tools/fonts`); every string the daemon sends goes through [`fit`] on its way out
//! ([`for_watch`]), so nothing reaches it as empty boxes or cut inside a character.
//!
//! - Common symbols get a stand-in ("→" is "->"), and Latin letters with marks outside Latin-1 lose
//!   their marks ("Zażółć" is "Zazółc").
//! - A run of letters the watch cannot draw (Cyrillic, Greek, CJK …) becomes one [`HIDDEN`] mark,
//!   which the watch draws as a glyph: text was here that it cannot show.
//! - Other symbols (emoji, joiners, variation selectors) are dropped with the space they leave.
//! - The result is cut to the field's byte limit ([`agentlink_proto::limits`]) and ends in `…`
//!   when cut. Pure text work.

use agentlink_proto::{limits, Attention, Message, Session};

/// Stands for a run of text the watch cannot draw: U+FFFD on the wire, drawn by the watch as a glyph.
pub const HIDDEN: char = '\u{fffd}';

/// Latin Extended-A (U+0100..U+017F) without its marks, one letter each.
const LATIN_EXT_A: &[u8; 128] = b"AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

/// A stand-in for a symbol the watch cannot draw.
fn stand_in(c: char) -> Option<&'static str> {
    Some(match c {
        '\n' => "\n",
        '\t' => "    ",
        '\u{a0}' | '\u{2002}'..='\u{200a}' | '\u{202f}' => " ",
        '\u{2192}' | '\u{27f6}' | '\u{279c}' | '\u{2794}' => "->",
        '\u{2190}' | '\u{27f5}' => "<-",
        '\u{2194}' => "<->",
        '\u{21d2}' | '\u{27f9}' => "=>",
        '\u{2265}' => ">=",
        '\u{2264}' => "<=",
        '\u{2260}' => "!=",
        '\u{2248}' => "~",
        '\u{2212}' | '\u{2010}' | '\u{2011}' => "-",
        '\u{2032}' => "'",
        '\u{2033}' => "\"",
        '\u{2713}' | '\u{2714}' | '\u{2705}' | '\u{2611}' => "[x]",
        '\u{274c}' | '\u{2717}' | '\u{2718}' | '\u{274e}' => "[ ]",
        '\u{25e6}' | '\u{2023}' | '\u{25aa}' | '\u{25b8}' | '\u{2219}' => "•",
        _ => return None,
    })
}

/// A Latin letter outside Latin-1 without its marks: Polish, Czech, Turkish, Romanian …
fn fold(c: char) -> Option<char> {
    match c {
        '\u{100}'..='\u{17f}' => Some(char::from(LATIN_EXT_A[c as usize - 0x100])),
        '\u{218}' => Some('S'),
        '\u{219}' => Some('s'),
        '\u{21a}' => Some('T'),
        '\u{21b}' => Some('t'),
        _ => None,
    }
}

/// The watch's glyph set, as `broker::detail_text` reads it.
fn shown(c: char) -> bool {
    matches!(c,
        '\u{20}'..='\u{7e}' | '\u{a1}'..='\u{ac}' | '\u{ae}'..='\u{ff}'
        | '\u{2013}' | '\u{2014}' | '\u{2018}' | '\u{2019}' | '\u{201c}' | '\u{201d}'
        | '\u{2022}' | '\u{2026}' | HIDDEN)
}

/// Only what the watch can draw (see the module docs). Line breaks are kept; "" stays "", and so does
/// text with nothing readable in it ("🎉").
pub fn drawable(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut dropped = false;
    for c in s.chars() {
        if let Some(t) = stand_in(c) {
            out.push_str(t);
        } else if let Some(f) = fold(c) {
            out.push(f);
        } else if shown(c) {
            // "✨ Done" reads "Done", not " Done"
            if !(dropped && c == ' ' && (out.is_empty() || out.ends_with([' ', '\n']))) {
                out.push(c);
            }
        } else if c.is_alphanumeric() {
            // one mark for the run, the spaces between its words included: "Привет мир" is "�"
            let tail = out.trim_end_matches(' ').len();
            if out[..tail].ends_with(HIDDEN) {
                out.truncate(tail);
            } else {
                out.push(HIDDEN);
            }
        } else {
            dropped = true;
            continue;
        }
        dropped = false;
    }
    out
}

/// `s` cut to `max` UTF-8 bytes on a character boundary, ending in `…` when cut.
pub fn cut(s: &str, max: usize) -> String {
    if s.len() <= max {
        return s.to_owned();
    }
    let room = max.saturating_sub('…'.len_utf8());
    let mut end = 0;
    for (i, c) in s.char_indices() {
        if i + c.len_utf8() > room {
            break;
        }
        end = i + c.len_utf8();
    }
    let mut out = s[..end].trim_end().to_owned();
    out.push('…');
    out
}

/// `s` as the watch shows it in a field of `max` bytes: drawable, cut, and never empty when `s` was
/// not (an all-emoji branch name is one [`HIDDEN`] mark, not a blank).
pub fn fit(s: &str, max: usize) -> String {
    let d = drawable(s);
    if d.trim().is_empty() && !s.trim().is_empty() {
        return HIDDEN.to_string();
    }
    cut(&d, max)
}

fn fit_in(s: &mut String, max: usize) {
    if !s.is_ascii() || s.len() > max {
        *s = fit(s, max);
    }
}

fn fit_opt(s: &mut Option<String>, max: usize) {
    if let Some(s) = s {
        fit_in(s, max);
    }
}

fn fit_all(v: &mut Option<Vec<String>>, max: usize) {
    for s in v.iter_mut().flatten() {
        fit_in(s, max);
    }
}

fn session(s: &mut Session) {
    fit_opt(&mut s.repo, limits::REPO);
    fit_opt(&mut s.br, limits::BRANCH);
    fit_opt(&mut s.tool, limits::TOOL);
    fit_opt(&mut s.sum, limits::SUMMARY);
    fit_opt(&mut s.hn, limits::HOST_NAME);
    fit_opt(&mut s.q, limits::QUESTION);
    fit_all(&mut s.rp, limits::REPLY);
    fit_opt(&mut s.mdl, limits::MODEL);
    fit_opt(&mut s.qd, limits::QUEUED);
    fit_opt(&mut s.lp, limits::LAST_PREVIEW);
}

/// `rule` is left alone: the watch grants it by its hash, so it must arrive exactly as saved (it is
/// printable ASCII, STANDING-RULES.md).
fn attention(a: &mut Attention) {
    fit_opt(&mut a.title, limits::TITLE);
    fit_opt(&mut a.text, limits::TEXT);
    fit_all(&mut a.opts, limits::OPTION);
    for q in a.qs.iter_mut().flatten() {
        fit_in(&mut q.h, limits::ASK_HEADER);
        fit_in(&mut q.q, limits::ASK_QUESTION);
        for o in &mut q.o {
            fit_in(&mut o.l, limits::ASK_LABEL);
            fit_opt(&mut o.d, limits::ASK_DESC);
        }
    }
}

/// Every display string in `msg` fitted to the watch. Answers come back as indices, never as text, so
/// this changes only what is shown. Not touched: a transcript (the daemon types exactly what it sent,
/// `voice::fit`), detail chunks (offsets into the reader's text) and the standing rule.
pub fn for_watch(msg: &mut Message) {
    match msg {
        Message::Welcome(w) => {
            fit_in(&mut w.host.name, limits::HOST);
            fit_all(&mut w.replies, limits::REPLY);
            fit_opt(&mut w.cfg.badge_handle, limits::HANDLE);
        }
        Message::Cfg(c) => fit_opt(&mut c.badge_handle, limits::HANDLE),
        Message::Snapshot(s) => s.sessions.iter_mut().for_each(session),
        Message::SessionUpd(s) => session(s),
        Message::Attention(a) => attention(a),
        Message::Result(r) => {
            fit_opt(&mut r.msg, limits::RESULT_MSG);
            if let Some(p) = &mut r.pos {
                fit_opt(&mut p.preview, limits::SCROLL_PREVIEW);
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_cut_ends_in_an_ellipsis_within_the_byte_limit() {
        let opt = "Yes, don't ask again for this command";
        assert_eq!(fit(opt, limits::OPTION), "Yes, don't ask again…");
        assert_eq!(fit("Yes", limits::OPTION), "Yes");
        // 2-byte letters: never half a character, and the mark still fits
        let e = "é".repeat(30);
        let f = fit(&e, 24);
        assert!(f.len() <= 24 && f.ends_with('…'), "{f}");
        assert_eq!(f, format!("{}…", "é".repeat(10)));
        let f = fit(&"x".repeat(40), 16);
        assert_eq!((f.len(), f.ends_with('…')), (16, true));
    }

    #[test]
    fn letters_the_watch_cannot_draw_become_one_mark_per_run() {
        assert_eq!(fit("сервер-сборки", 16), "\u{fffd}-\u{fffd}");
        assert_eq!(fit("Привет мир!", 40), "\u{fffd}!");
        assert_eq!(fit("fix 登录页面 bug", 40), "fix \u{fffd} bug");
        assert_eq!(fit("Αθήνα", 40), "\u{fffd}");
    }

    #[test]
    fn latin_letters_lose_marks_the_watch_cannot_draw() {
        assert_eq!(fit("Zażółć gęślą jaźń", 40), "Zazólc gesla jazn");
        assert_eq!(fit("Příliš žluťoučký", 40), "Prílis zlutoucký");
        assert_eq!(fit("İstanbul ığdır şehir", 40), "Istanbul igdir sehir");
        assert_eq!(fit("Œuvre ştiinţă ș ț", 40), "Ouvre stiinta s t");
        assert_eq!(LATIN_EXT_A[0x142 - 0x100], b'l'); // ł
        assert_eq!(LATIN_EXT_A[0x17e - 0x100], b'z'); // ž
    }

    #[test]
    fn symbols_get_stand_ins_or_go_and_never_leave_a_blank() {
        assert_eq!(fit("✨ feat: login → done", 40), "feat: login -> done");
        assert_eq!(fit(&"🚀".repeat(26), 32), "\u{fffd}");
        assert_eq!(fit("", 32), "", "empty clears a field");
        assert_eq!(drawable("🎉"), "", "the reader's nothing-left rule");
    }

    #[test]
    fn fitting_twice_changes_nothing() {
        for s in [
            "Yes, don't ask again for this command",
            "сервер-сборки",
            "Zażółć",
            "🚀🚀",
        ] {
            let once = fit(s, 16);
            assert_eq!(fit(&once, 16), once, "{s}");
        }
    }

    #[test]
    fn every_string_of_a_message_is_fitted_except_the_rule() {
        let mut s = Session::delta(1);
        s.repo = Some("репозиторий".into());
        s.br = Some("feature/a-very-long-branch-name-indeed".into());
        s.hn = Some("WIN-BUILD-SERVER-0".into());
        let mut m = Message::SessionUpd(s);
        for_watch(&mut m);
        let Message::SessionUpd(s) = m else {
            unreachable!()
        };
        assert_eq!(s.repo.as_deref(), Some("\u{fffd}"));
        assert_eq!(s.br.as_deref(), Some("feature/a-very-long-branch-na…"));
        assert_eq!(s.hn.as_deref(), Some("WIN-BUILD-SER…"));
    }

    /// The byte at which `anchor` ends in `src`, and the number after it.
    fn cap(src: &str, anchor: &str) -> usize {
        let at = src
            .find(anchor)
            .unwrap_or_else(|| panic!("{anchor} not found"))
            + anchor.len();
        let n: String = src[at..].chars().take_while(char::is_ascii_digit).collect();
        n.parse().unwrap_or_else(|_| panic!("{anchor}: no size"))
    }

    #[test]
    fn the_watch_holds_every_limit() {
        let model = include_str!("../../../../firmware/components/core/src/model.h");
        let model_c = include_str!("../../../../firmware/components/core/src/model.c");
        let rx = include_str!("../../../../firmware/components/core/src/link_rx.c");
        let cases = [
            (model, "char repo[", limits::REPO),
            (model, "branch[", limits::BRANCH),
            (model, "tool[", limits::TOOL),
            (model, "summary[", limits::SUMMARY),
            (model, "char hn[", limits::HOST_NAME),
            (model, "char q[", limits::QUESTION),
            (model, "char rp[3][", limits::REPLY),
            (model, "char mdl[", limits::MODEL),
            (model, "char queued[", limits::QUEUED),
            (model, "char lp[", limits::LAST_PREVIEW),
            (model, "char title[", limits::TITLE),
            (model, ", text[", limits::TEXT),
            (model, "opts[4][", limits::OPTION),
            (model, "char h[", limits::ASK_HEADER),
            (model, "char h[20], q[", limits::ASK_QUESTION),
            (model, "label[MODEL_MAX_QOPTS][", limits::ASK_LABEL),
            (model, "desc[MODEL_MAX_QOPTS][", limits::ASK_DESC),
            (model, "char handle[", limits::HANDLE),
            (model_c, "s_host[", limits::HOST),
            (model_c, "s_replies[MODEL_MAX_REPLIES][", limits::REPLY),
            (rx, "char host[", limits::HOST),
            (rx, "buf[MODEL_MAX_REPLIES][", limits::REPLY),
            (rx, "\n    char msg[", limits::RESULT_MSG),
            (rx, "char preview[", limits::SCROLL_PREVIEW),
        ];
        for (src, anchor, limit) in cases {
            assert!(
                cap(src, anchor) > limit,
                "{anchor} holds less than {limit} bytes"
            );
        }
    }
}
