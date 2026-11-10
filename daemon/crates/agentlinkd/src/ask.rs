//! The agent's closing question, for the watch's Reply list (proto 0.5).
//!
//! When a turn ends, Claude Code's Stop hook carries the turn's last message
//! (`last_assistant_message`). If it closes on a question, the watch shows that question above
//! the Reply list and offers answers that fit it: the options of an "A or B?" question, or
//! "Yes, go ahead" / "No" for a yes/no one. An open question ("Which one?") gets no answers of its
//! own; the user's fixed replies still follow. Pure text work: no model, no network.

use crate::registry::clip;

pub const MAX_QUESTION: usize = 120;
pub const MAX_CHOICES: usize = 3;
pub const MAX_CHOICE: usize = 24;

/// Words that open a yes/no question.
const YES_NO: &[&str] = &[
    "should", "shall", "do", "does", "did", "can", "could", "would", "will", "want", "is", "are",
    "was", "may", "ok", "okay", "ready", "go",
];

/// Leading words of a question that are not part of its first option.
const LEAD: &[&str] = &[
    "should", "shall", "do", "does", "did", "can", "could", "would", "will", "want", "is", "are",
    "i", "it", "we", "you", "me", "to", "they", "this", "that", "prefer", "like", "rather",
];

/// Markdown and list noise removed before reading the text.
fn plain(line: &str) -> String {
    let t = line.trim().trim_start_matches(['-', '*', '>', '#', ' ']);
    t.replace("**", "").replace('`', "").replace("__", "")
}

/// The last sentence ending in "?" within the closing paragraph(s) of `text`.
fn closing_question(text: &str) -> Option<String> {
    let paras: Vec<String> = text
        .split("\n\n")
        .map(|p| p.lines().map(plain).collect::<Vec<_>>().join(" "))
        .map(|p| p.trim().to_owned())
        .filter(|p| !p.is_empty())
        .collect();
    // only the end of the message counts: a question asked mid-way was not what the turn ended on
    for p in paras.iter().rev().take(2) {
        if let Some(end) = p.rfind('?') {
            let head = &p[..end];
            let start = head.rfind(['.', '!', '?', ':']).map(|i| i + 1).unwrap_or(0);
            let q = format!("{}?", head[start..].trim());
            if q.len() > 3 {
                return Some(q);
            }
        }
    }
    None
}

fn capitalise(s: &str) -> String {
    let mut c = s.chars();
    match c.next() {
        Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
        None => String::new(),
    }
}

/// "Do you want A, B or C?" -> [A, B, C] when each option is short.
fn either_or(q: &str) -> Option<Vec<String>> {
    let body = q.trim_end_matches('?');
    let (left, last) = body.rsplit_once(" or ")?;
    // the options start after the question's verb phrase: take the left side's last clause
    let left = left
        .rsplit_once(" between ")
        .or_else(|| left.rsplit_once(": "))
        .map(|(_, l)| l)
        .unwrap_or(left);
    let mut opts: Vec<&str> = left.split(", ").collect();
    let first = opts.remove(0);
    // "Should it use USB" -> "use USB": the question's own words are not part of the option
    let first: String = first
        .split_whitespace()
        .skip_while(|w| LEAD.contains(&w.to_ascii_lowercase().as_str()))
        .collect::<Vec<_>>()
        .join(" ");
    let mut all = vec![first];
    all.extend(opts.into_iter().map(str::to_owned));
    all.push(last.trim().trim_start_matches("rather ").to_owned());
    let all: Vec<String> = all
        .into_iter()
        .map(|o| o.trim().trim_end_matches(',').to_owned())
        .filter(|o| !o.is_empty())
        .collect();
    let short = all.len() >= 2
        && all.len() <= MAX_CHOICES
        && all
            .iter()
            .all(|o| o.split_whitespace().count() <= 4 && o.chars().count() <= MAX_CHOICE);
    short.then(|| all.iter().map(|o| capitalise(o)).collect())
}

fn yes_no(q: &str) -> bool {
    let first = q
        .split_whitespace()
        .next()
        .unwrap_or("")
        .trim_matches(|c: char| !c.is_alphanumeric())
        .to_ascii_lowercase();
    YES_NO.contains(&first.as_str())
}

/// (question, answers) for a turn that ended with `text`; ("", []) when it did not end on a question.
pub fn ask_of(text: &str) -> (String, Vec<String>) {
    let Some(q) = closing_question(text) else {
        return (String::new(), Vec::new());
    };
    let choices = either_or(&q).unwrap_or_else(|| {
        if yes_no(&q) {
            vec!["Yes, go ahead".into(), "No".into()]
        } else {
            Vec::new()
        }
    });
    let choices = choices
        .into_iter()
        .map(|c| clip(&c, MAX_CHOICE))
        .take(MAX_CHOICES)
        .collect();
    (clip(&q, MAX_QUESTION), choices)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_yes_no_question_at_the_end() {
        let t = "I fixed it.\n\nTo make the fix live, I'd flash the watch.\nShould I do that now?";
        let (q, a) = ask_of(t);
        assert_eq!(q, "Should I do that now?");
        assert_eq!(a, vec!["Yes, go ahead", "No"]);
    }

    #[test]
    fn markdown_is_stripped() {
        let (q, a) = ask_of("Done.\n\n**Next:** I can start on B2. Want me to **start on it**?");
        assert_eq!(q, "Want me to start on it?");
        assert_eq!(a.len(), 2);
    }

    #[test]
    fn either_or_offers_the_options() {
        let (q, a) = ask_of("Two ways.\n\nShould it use USB or BLE?");
        assert_eq!(q, "Should it use USB or BLE?");
        assert_eq!(a, vec!["Use USB", "BLE"]);
        let (_, a) = ask_of("Pick one: tests, docs or both?");
        assert_eq!(a, vec!["Tests", "Docs", "Both"]);
    }

    #[test]
    fn long_options_fall_back_to_yes_no() {
        let (_, a) = ask_of("Should I rewrite the whole scroll path from scratch or patch the existing driver in place?");
        assert_eq!(a, vec!["Yes, go ahead", "No"]);
    }

    #[test]
    fn an_open_question_has_no_answers() {
        let (q, a) = ask_of("Which terminal do you use most?");
        assert_eq!(q, "Which terminal do you use most?");
        assert!(a.is_empty());
    }

    #[test]
    fn no_question_or_one_asked_earlier() {
        assert_eq!(ask_of("All tests pass. Pushed."), (String::new(), vec![]));
        let t = "Should I keep going? I did.\n\nPara two.\n\nPara three.\n\nAll done.";
        assert_eq!(
            ask_of(t).0,
            "",
            "a question three paragraphs back is not the closing one"
        );
    }

    #[test]
    fn bounded() {
        let long = format!("Should I {}?", "really ".repeat(40));
        let (q, a) = ask_of(&long);
        assert!(q.chars().count() <= MAX_QUESTION);
        assert!(a.iter().all(|c| c.chars().count() <= MAX_CHOICE) && a.len() <= MAX_CHOICES);
    }
}
