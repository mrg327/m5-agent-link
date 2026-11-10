//! KEYS swipe keyboard, daemon side (issue 20261110-1e57, PROTOCOL §6a).
//!
//! The watch draws the keys and sends their rectangles (`kb_start`) and then one touch stroke per
//! word (`kb_stroke`). The daemon decodes the stroke (agentlink-keys), types the best word into the
//! session's terminal and answers `kb_cands`. `kb_pick` swaps the last word for another candidate,
//! `kb_del` erases it, `kb_send` presses Enter after a hold, `kb_end` closes the keyboard.
//!
//! The terminal only ever holds the words typed so far, so every change is "erase the last word,
//! type another": `Words` keeps what was typed and says how many backspaces an edit needs.
//!
//! Taps: a tap stroke is a letter of the current tap-word. Each tap re-decodes the whole run of taps
//! (`decode_taps`) and replaces the word it typed before, so the word being tapped is always
//! on screen in its best spelling. A swipe, a space tap or a send ends the run; a backspace tap
//! is `kb_del`. Other keys (layer switch, punctuation) type nothing.
//!
//! Privacy: strokes, keys, candidates and words never reach the logs; only counts and timings do.

use std::sync::{Arc, Mutex};
use std::time::Instant;

use agentlink_keys::{unpack_points, Context, Decoder, Layout, Lexicon, Point};
use agentlink_proto::{Arg, CmdOp, CmdResult, KbCands, KbStart, KbStroke, ResultErr, Session};
use tokio::task::JoinHandle;

use crate::focus::Terminal;
use crate::registry::HostCtx;

/// `kb_send` needs the same deliberate hold as an approval.
const MIN_SEND_HOLD_MS: u64 = 1000;
const CANDS: usize = 4;
/// Source files named in the repository that feed the decoder's vocabulary.
const MAX_FILES: usize = 5000;
/// Screen text and sent words that may feed the vocabulary.
const MAX_SCREEN_CHARS: usize = 8000;
const MAX_RECENT: usize = 40;
const CP_SPACE: u32 = 0x20;
const CP_BACKSPACE: u32 = 0x08;

/// Where a keyboard action goes: the session, its terminal context and why it must not be typed
/// into right now, if so (typing.rs).
pub struct Target {
    pub session: Session,
    pub ctx: HostCtx,
    pub refusal: Option<&'static str>,
}

/// Words typed into the terminal and not yet sent.
#[derive(Default, Debug)]
struct Words(Vec<String>);

/// A change to the terminal: `erase` backspaces, then `text`.
#[derive(Debug, PartialEq, Eq)]
struct Edit {
    erase: usize,
    text: String,
}

impl Words {
    fn typed(&self, i: usize) -> String {
        if i == 0 {
            self.0[0].clone()
        } else {
            format!(" {}", self.0[i])
        }
    }

    fn push(&self, word: &str) -> Edit {
        let text = if self.0.is_empty() {
            word.to_owned()
        } else {
            format!(" {word}")
        };
        Edit { erase: 0, text }
    }

    fn replace_last(&self, word: &str) -> Edit {
        let Some(i) = self.0.len().checked_sub(1) else {
            return self.push(word);
        };
        let lead = if i == 0 { "" } else { " " };
        Edit {
            erase: self.typed(i).chars().count(),
            text: format!("{lead}{word}"),
        }
    }

    fn pop(&self) -> Edit {
        Edit {
            erase: self
                .0
                .len()
                .checked_sub(1)
                .map_or(0, |i| self.typed(i).chars().count()),
            text: String::new(),
        }
    }

    fn all(&self) -> Edit {
        Edit {
            erase: (0..self.0.len())
                .map(|i| self.typed(i).chars().count())
                .sum(),
            text: String::new(),
        }
    }
}

/// The decoder and what it was told about the conversation.
struct Engine {
    decoder: Decoder,
    ctx: Context,
    /// `ctx.text` changed since the decoder last saw it.
    dirty: bool,
}

enum Build {
    Pending(JoinHandle<Engine>),
    Ready(Arc<Mutex<Engine>>),
}

struct Comp {
    kid: u64,
    sid: u64,
    build: Build,
    words: Words,
    last_cands: Vec<String>,
    taps: Vec<Point>,
    /// The last word typed came from `taps` and the next tap extends it.
    tap_open: bool,
    /// The symbols and words glued before the decoded part of the last word (`check_dram.` in
    /// `check_dram.py`); a pick replaces only what follows it.
    prefix: String,
    /// The last tap was a symbol: the next word joins the last one without a space.
    glue: bool,
}

impl Comp {
    async fn engine(&mut self) -> Option<Arc<Mutex<Engine>>> {
        if let Build::Pending(h) = &mut self.build {
            let e = h.await.ok()?;
            self.build = Build::Ready(Arc::new(Mutex::new(e)));
        }
        match &self.build {
            Build::Ready(e) => Some(e.clone()),
            Build::Pending(_) => None,
        }
    }

    fn end_tap_run(&mut self) {
        self.taps.clear();
        self.tap_open = false;
    }

    fn reset_glue(&mut self) {
        self.prefix.clear();
        self.glue = false;
    }
}

/// Keyboard state of one device link.
#[derive(Default)]
pub struct KeysLink {
    comp: Option<Comp>,
    /// Words sent with Enter, newest last: vocabulary for the next composition.
    recent: Vec<String>,
}

fn clean(mut cands: Vec<String>) -> Vec<String> {
    // candidates may come from terminal text: never let one carry control characters
    cands.retain(|w| !w.is_empty() && w.chars().all(|c| !c.is_control() && !c.is_whitespace()));
    cands.truncate(CANDS);
    cands
}

fn map_err(m: &str) -> ResultErr {
    if m.contains("cannot be typed") {
        ResultErr::Unsupported
    } else if m.contains("no window") {
        ResultErr::NoWindow
    } else {
        ResultErr::Denied
    }
}

/// Context for the decoder: the agent's question, the terminal screen, the repository's file
/// names and the words sent recently. Blocking (kitty and git are run as processes).
fn gather(term: &dyn Terminal, t: &Target, recent: &[String]) -> Context {
    let mut text = Vec::new();
    if let Some(q) = &t.session.q {
        text.push(q.clone());
    }
    if let Ok(screen) = term.read_screen(&t.ctx) {
        let n = screen.chars().count();
        text.push(
            screen
                .chars()
                .skip(n.saturating_sub(MAX_SCREEN_CHARS))
                .collect(),
        );
    }
    text.extend(repo_files(t.ctx.cwd.as_deref()));
    if !recent.is_empty() {
        text.push(recent.join(" "));
    }
    Context {
        text,
        prev_word: None,
    }
}

fn repo_files(cwd: Option<&str>) -> Vec<String> {
    let Some(cwd) = cwd.filter(|c| std::path::Path::new(c).is_absolute()) else {
        return Vec::new();
    };
    let args: Vec<String> = ["-c", "core.fsmonitor=false", "-C", cwd, "ls-files"]
        .map(String::from)
        .into();
    match crate::focus::run("git", &args) {
        Ok(out) if out.ok => out
            .stdout
            .lines()
            .take(MAX_FILES)
            .filter_map(|l| l.rsplit('/').next())
            .map(str::to_owned)
            .collect(),
        _ => Vec::new(),
    }
}

fn spawn_build(layout: Layout, ctx: Context) -> JoinHandle<Engine> {
    tokio::task::spawn_blocking(move || {
        let t0 = Instant::now();
        let mut decoder = Decoder::new(layout, Lexicon::english());
        decoder.set_context(&ctx);
        tracing::info!(
            "keyboard decoder built in {} ms ({} context texts)",
            t0.elapsed().as_millis(),
            ctx.text.len()
        );
        Engine {
            decoder,
            ctx,
            dirty: false,
        }
    })
}

fn cands_reply(
    kid: u64,
    seq: u64,
    cands: Vec<String>,
    typed: Option<u64>,
    err: Option<ResultErr>,
) -> KbCands {
    KbCands {
        kid,
        seq,
        cands,
        typed,
        err,
    }
}

impl KeysLink {
    /// The session whose keyboard is open as `kid`.
    pub fn sid_of(&self, kid: u64) -> Option<u64> {
        self.comp.as_ref().filter(|c| c.kid == kid).map(|c| c.sid)
    }

    /// `kb_start`. The decoder is built in the background, so the link keeps running; the first
    /// stroke waits for it. A repeated start with the same `kid` swaps the layout and keeps the
    /// composition. Returns false when the layout is unusable.
    pub async fn start(&mut self, term: &Arc<dyn Terminal>, t: &Target, kb: &KbStart) -> bool {
        let Ok(layout) = Layout::from_packed(&kb.keys) else {
            self.comp = None;
            return false;
        };
        if let Some(c) = self
            .comp
            .as_mut()
            .filter(|c| c.kid == kb.kid && c.sid == kb.sid)
        {
            let ctx = match c.engine().await {
                Some(e) => e.lock().unwrap().ctx.clone(),
                None => Context::default(),
            };
            c.build = Build::Pending(spawn_build(layout, ctx));
            return true;
        }
        let (term, ctx, recent) = (term.clone(), t.ctx.clone(), self.recent.clone());
        let session = t.session.clone();
        let build = Build::Pending(tokio::task::spawn_blocking(move || {
            let t = Target {
                session,
                ctx,
                refusal: None,
            };
            let ctx = gather(term.as_ref(), &t, &recent);
            let mut decoder = Decoder::new(layout, Lexicon::english());
            decoder.set_context(&ctx);
            Engine {
                decoder,
                ctx,
                dirty: false,
            }
        }));
        self.comp = Some(Comp {
            kid: kb.kid,
            sid: kb.sid,
            build,
            words: Words::default(),
            last_cands: Vec::new(),
            taps: Vec::new(),
            tap_open: false,
            prefix: String::new(),
            glue: false,
        });
        true
    }

    /// `kb_stroke`: decode, type the best word, answer with the candidates.
    pub async fn stroke(&mut self, term: &Arc<dyn Terminal>, t: &Target, kb: &KbStroke) -> KbCands {
        let (kid, seq) = (kb.kid, kb.seq);
        let stale = || cands_reply(kid, seq, Vec::new(), None, Some(ResultErr::Stale));
        let Some(comp) = self.comp.as_mut().filter(|c| c.kid == kid) else {
            return stale();
        };
        let Ok(pts) = unpack_points(&kb.pts) else {
            return cands_reply(kid, seq, Vec::new(), None, Some(ResultErr::Denied));
        };
        if pts.is_empty() {
            return cands_reply(kid, seq, Vec::new(), None, None);
        }
        let Some(engine) = comp.engine().await else {
            self.comp = None;
            return stale();
        };

        let first = pts[0];
        let tap = Decoder::is_tap(&pts);
        let mut taps = Vec::new();
        if tap {
            let cp = {
                let e = engine.lock().unwrap();
                e.decoder.layout().key_at(pts[0].x, pts[0].y).map(|k| k.cp)
            };
            match cp {
                Some(CP_SPACE) => {
                    comp.end_tap_run();
                    comp.glue = false;
                    return cands_reply(kid, seq, comp.last_cands.clone(), None, None);
                }
                Some(CP_BACKSPACE) => {
                    let res = self.del(term, t).await;
                    return cands_reply(kid, seq, Vec::new(), None, res.err);
                }
                Some(c)
                    if char::from_u32(c)
                        .is_some_and(|c| c.is_ascii_graphic() && !c.is_alphabetic()) =>
                {
                    let sym = char::from_u32(c).expect("checked in the guard");
                    let joined = match comp.words.0.last() {
                        Some(last) => format!("{last}{sym}"),
                        None => sym.to_string(),
                    };
                    let edit = if comp.words.0.is_empty() {
                        comp.words.push(&joined)
                    } else {
                        comp.words.replace_last(&joined)
                    };
                    if let Some(why) = t.refusal {
                        tracing::info!("keyboard typing refused: {why}");
                        return cands_reply(kid, seq, Vec::new(), None, Some(ResultErr::Denied));
                    }
                    if let Err(e) = apply(term, t, &edit).await {
                        return cands_reply(kid, seq, Vec::new(), None, Some(e.0));
                    }
                    match comp.words.0.last_mut() {
                        Some(last) => *last = joined.clone(),
                        None => comp.words.0.push(joined.clone()),
                    }
                    comp.end_tap_run();
                    comp.prefix = joined;
                    comp.glue = true;
                    comp.last_cands.clear();
                    return cands_reply(kid, seq, Vec::new(), None, None);
                }
                Some(c) if char::from_u32(c).is_some_and(|c| c.is_alphabetic()) => {
                    taps = comp.taps.clone();
                    taps.push(pts[0]);
                }
                _ => return cands_reply(kid, seq, comp.last_cands.clone(), None, None),
            }
        }

        let replace = tap && comp.tap_open && !comp.words.0.is_empty();
        let prev = if replace {
            comp.words.0.len().checked_sub(2)
        } else {
            comp.words.0.len().checked_sub(1)
        }
        .map(|i| comp.words.0[i].clone());
        let t0 = Instant::now();
        let cands = tokio::task::spawn_blocking(move || {
            let mut e = engine.lock().unwrap();
            if e.dirty || e.ctx.prev_word != prev {
                e.ctx.prev_word = prev;
                e.dirty = false;
                let ctx = e.ctx.clone();
                e.decoder.set_context(&ctx);
            }
            let found = if tap {
                e.decoder.decode_taps(&taps, CANDS)
            } else {
                e.decoder.decode_stroke(&pts, CANDS)
            };
            clean(found.into_iter().map(|c| c.word).collect())
        })
        .await
        .unwrap_or_default();
        tracing::info!(
            "keyboard stroke {seq} decoded in {} ms: {} candidates",
            t0.elapsed().as_millis(),
            cands.len()
        );

        let comp = self.comp.as_mut().expect("kept across the awaits");
        let Some(word) = cands.first().cloned() else {
            return cands_reply(kid, seq, cands, None, None);
        };
        if let Some(why) = t.refusal {
            tracing::info!("keyboard typing refused: {why}");
            return cands_reply(kid, seq, cands, None, Some(ResultErr::Denied));
        }
        let joins = replace || comp.glue;
        let full = if joins {
            format!("{}{word}", comp.prefix)
        } else {
            word.clone()
        };
        let edit = if joins {
            comp.words.replace_last(&full)
        } else {
            comp.words.push(&full)
        };
        if let Err(e) = apply(term, t, &edit).await {
            return cands_reply(kid, seq, cands, None, Some(e.0));
        }
        if joins {
            *comp.words.0.last_mut().expect("a join needs a word") = full;
        } else {
            comp.prefix.clear();
            comp.words.0.push(full);
        }
        comp.glue = false;
        if tap {
            comp.taps.push(first);
            comp.tap_open = true;
        } else {
            comp.end_tap_run();
        }
        comp.last_cands = cands.clone();
        cands_reply(kid, seq, cands, Some(0), None)
    }

    /// `kb_pick`, `kb_del`, `kb_send`, `kb_end`.
    pub async fn cmd(
        &mut self,
        term: &Arc<dyn Terminal>,
        t: &Target,
        op: CmdOp,
        arg: Option<Arg>,
        hold_ms: Option<u64>,
    ) -> CmdResult {
        let Some(comp) = self.comp.as_mut().filter(|c| c.sid == t.session.sid) else {
            return if op == CmdOp::KbEnd {
                CmdResult::ok()
            } else {
                fail(ResultErr::Stale, "no keyboard open")
            };
        };
        match op {
            CmdOp::KbPick => {
                let Some(Arg::Uint(i)) = arg else {
                    return fail(ResultErr::Denied, "kb_pick needs a candidate index");
                };
                let Some(word) = usize::try_from(i)
                    .ok()
                    .and_then(|i| comp.last_cands.get(i))
                    .cloned()
                else {
                    return fail(ResultErr::Denied, "no such candidate");
                };
                if comp.words.0.is_empty() {
                    return fail(ResultErr::Stale, "nothing to replace");
                }
                let word = format!("{}{word}", comp.prefix);
                let edit = comp.words.replace_last(&word);
                match apply_checked(term, t, &edit).await {
                    Ok(()) => {
                        *comp.words.0.last_mut().expect("checked above") = word;
                        comp.end_tap_run();
                        CmdResult::ok()
                    }
                    Err(e) => e,
                }
            }
            CmdOp::KbDel => self.del(term, t).await,
            CmdOp::KbSend => {
                if hold_ms.unwrap_or(0) < MIN_SEND_HOLD_MS {
                    return fail(ResultErr::Denied, "hold-to-confirm too short");
                }
                if comp.words.0.is_empty() {
                    return fail(ResultErr::Denied, "nothing typed");
                }
                if let Some(why) = t.refusal {
                    return fail(ResultErr::Denied, why);
                }
                let (tm, ctx) = (term.clone(), t.ctx.clone());
                let res = tokio::task::spawn_blocking(move || {
                    crate::typing::press_enter(tm.as_ref(), &ctx)
                })
                .await
                .unwrap_or_else(|e| Err(e.to_string()));
                if let Err(m) = res {
                    return fail(map_err(&m), &m);
                }
                let words = std::mem::take(&mut comp.words.0);
                tracing::info!("keyboard send: {} words", words.len());
                comp.end_tap_run();
                comp.reset_glue();
                comp.last_cands.clear();
                self.remember(&words).await;
                CmdResult::ok()
            }
            CmdOp::KbEnd => {
                let erase = matches!(arg, Some(Arg::Uint(1)));
                let edit = comp.words.all();
                let kid = comp.kid;
                let res = if erase && edit.erase > 0 {
                    apply_checked(term, t, &edit).await
                } else {
                    Ok(())
                };
                tracing::info!("keyboard closed (erase: {erase}, {} chars)", edit.erase);
                if self.comp.as_ref().is_some_and(|c| c.kid == kid) {
                    self.comp = None;
                }
                res.map_or_else(|e| e, |()| CmdResult::ok())
            }
            _ => fail(ResultErr::Unsupported, "not a keyboard command"),
        }
    }

    async fn del(&mut self, term: &Arc<dyn Terminal>, t: &Target) -> CmdResult {
        let Some(comp) = self.comp.as_mut().filter(|c| c.sid == t.session.sid) else {
            return fail(ResultErr::Stale, "no keyboard open");
        };
        if comp.words.0.is_empty() {
            return CmdResult::ok();
        }
        let edit = comp.words.pop();
        match apply_checked(term, t, &edit).await {
            Ok(()) => {
                comp.words.0.pop();
                comp.end_tap_run();
                comp.reset_glue();
                comp.last_cands.clear();
                CmdResult::ok()
            }
            Err(e) => e,
        }
    }

    /// Sent words are vocabulary for what the user types next.
    async fn remember(&mut self, words: &[String]) {
        self.recent.extend(words.iter().cloned());
        let n = self.recent.len();
        self.recent.drain(..n.saturating_sub(MAX_RECENT));
        if let Some(c) = self.comp.as_mut() {
            if let Some(e) = c.engine().await {
                let mut e = e.lock().unwrap();
                e.ctx.text.push(words.join(" "));
                e.dirty = true;
            }
        }
    }
}

fn fail(err: ResultErr, msg: &str) -> CmdResult {
    CmdResult {
        ok: false,
        err: Some(err),
        msg: Some(msg.to_owned()),
        pos: None,
    }
}

async fn apply(
    term: &Arc<dyn Terminal>,
    t: &Target,
    edit: &Edit,
) -> Result<(), (ResultErr, String)> {
    let (tm, ctx) = (term.clone(), t.ctx.clone());
    let (erase, text) = (edit.erase, edit.text.clone());
    tokio::task::spawn_blocking(move || crate::typing::edit_text(tm.as_ref(), &ctx, erase, &text))
        .await
        .unwrap_or_else(|e| Err(e.to_string()))
        .map_err(|m| (map_err(&m), m))
}

/// An edit made on the user's behalf, refused like any other typing.
async fn apply_checked(term: &Arc<dyn Terminal>, t: &Target, edit: &Edit) -> Result<(), CmdResult> {
    if let Some(why) = t.refusal {
        return Err(fail(ResultErr::Denied, why));
    }
    apply(term, t, edit).await.map_err(|(e, m)| fail(e, &m))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::focus::FocusError;
    use agentlink_keys::pack_points;
    use agentlink_keys::synth::{synth_stroke, synth_taps, Rng, SynthParams};

    #[derive(Default)]
    struct Fake {
        sent: Mutex<Vec<String>>,
    }
    impl Terminal for Fake {
        fn read_screen(&self, _: &HostCtx) -> Result<String, FocusError> {
            Ok("> \n".into())
        }
        fn send_keys(&self, _: &HostCtx, k: &str) -> Result<(), FocusError> {
            self.sent.lock().unwrap().push(k.to_owned());
            Ok(())
        }
    }

    fn target(refusal: Option<&'static str>) -> Target {
        Target {
            session: Session::delta(7),
            ctx: HostCtx::default(),
            refusal,
        }
    }

    async fn open() -> (KeysLink, Arc<Fake>, Arc<dyn Terminal>, Layout) {
        let fake = Arc::new(Fake::default());
        let term: Arc<dyn Terminal> = fake.clone();
        let layout = Layout::stopwatch_qwerty();
        let mut k = KeysLink::default();
        let start = KbStart {
            kid: 1,
            sid: 7,
            keys: layout.to_packed(),
        };
        assert!(k.start(&term, &target(None), &start).await);
        (k, fake, term, layout)
    }

    fn swipe(layout: &Layout, word: &str, seq: u64) -> KbStroke {
        let pts = synth_stroke(layout, word, &mut Rng::new(seq), &SynthParams::default()).unwrap();
        KbStroke {
            kid: 1,
            seq,
            pts: pack_points(&pts),
        }
    }

    fn sent(f: &Fake) -> Vec<String> {
        std::mem::take(&mut *f.sent.lock().unwrap())
    }

    #[test]
    fn edits_erase_exactly_what_was_typed() {
        let mut w = Words::default();
        assert_eq!(
            w.push("hello"),
            Edit {
                erase: 0,
                text: "hello".into()
            }
        );
        w.0.push("hello".into());
        assert_eq!(w.push("world").text, " world");
        w.0.push("world".into());
        assert_eq!(
            w.replace_last("word"),
            Edit {
                erase: 6,
                text: " word".into()
            }
        );
        assert_eq!(w.pop().erase, 6);
        assert_eq!(w.all().erase, 11);
        w.0.pop();
        assert_eq!(
            w.replace_last("jello"),
            Edit {
                erase: 5,
                text: "jello".into()
            }
        );
    }

    #[tokio::test]
    async fn a_stroke_types_the_best_word_and_a_pick_replaces_it() {
        let (mut k, fake, term, layout) = open().await;
        let t = target(None);
        let r = k.stroke(&term, &t, &swipe(&layout, "hello", 1)).await;
        assert_eq!((r.err, r.typed), (None, Some(0)));
        assert!(r.cands.len() >= 2 && r.cands.len() <= CANDS);
        assert_eq!(sent(&fake), vec![r.cands[0].clone()]);

        let r2 = k.stroke(&term, &t, &swipe(&layout, "world", 2)).await;
        assert_eq!(sent(&fake), vec![format!(" {}", r2.cands[0])]);

        let res = k
            .cmd(&term, &t, CmdOp::KbPick, Some(Arg::Uint(1)), None)
            .await;
        assert!(res.ok);
        let back = "\\x7f".repeat(1 + r2.cands[0].chars().count());
        assert_eq!(sent(&fake), vec![back, format!(" {}", r2.cands[1])]);

        let res = k
            .cmd(&term, &t, CmdOp::KbPick, Some(Arg::Uint(9)), None)
            .await;
        assert_eq!(res.err, Some(ResultErr::Denied));
    }

    #[tokio::test]
    async fn taps_extend_one_word_and_delete_erases_it() {
        let (mut k, fake, term, layout) = open().await;
        let t = target(None);
        let taps = synth_taps(&layout, "hi", &mut Rng::new(3), &SynthParams::default()).unwrap();
        let mut last = Vec::new();
        for (i, p) in taps.iter().enumerate() {
            let kb = KbStroke {
                kid: 1,
                seq: i as u64,
                pts: pack_points(&[*p]),
            };
            last = k.stroke(&term, &t, &kb).await.cands;
        }
        let keys = sent(&fake);
        assert_eq!(keys.len(), 3, "first letter, then erase + retype: {keys:?}");
        assert_eq!(keys[2], last[0]);
        assert!(k.cmd(&term, &t, CmdOp::KbDel, None, None).await.ok);
        assert_eq!(sent(&fake), vec!["\\x7f".repeat(last[0].chars().count())]);
    }

    #[tokio::test]
    async fn a_symbol_glues_the_words_around_it_and_a_pick_keeps_it() {
        let fake = Arc::new(Fake::default());
        let term: Arc<dyn Terminal> = fake.clone();
        let mut keys = Layout::stopwatch_qwerty().keys().to_vec();
        keys.push(agentlink_keys::Key {
            cp: '.' as u32,
            x: 200,
            y: 400,
            w: 40,
            h: 40,
        });
        let layout = Layout::new(keys);
        let mut k = KeysLink::default();
        let t = target(None);
        let start = KbStart {
            kid: 1,
            sid: 7,
            keys: layout.to_packed(),
        };
        assert!(k.start(&term, &t, &start).await);

        let a = k.stroke(&term, &t, &swipe(&layout, "check", 1)).await.cands[0].clone();
        let dot = KbStroke {
            kid: 1,
            seq: 2,
            pts: pack_points(&[Point {
                x: 220,
                y: 420,
                t_ms: 0,
            }]),
        };
        assert_eq!(k.stroke(&term, &t, &dot).await.err, None);
        let r = k.stroke(&term, &t, &swipe(&layout, "dram", 3)).await;
        let b = r.cands[0].clone();
        let back = |n: usize| "\\x7f".repeat(n);
        assert_eq!(
            sent(&fake),
            vec![
                a.clone(),
                back(a.len()),
                format!("{a}."),
                back(a.len() + 1),
                format!("{a}.{b}")
            ]
        );
        assert!(
            k.cmd(&term, &t, CmdOp::KbPick, Some(Arg::Uint(1)), None)
                .await
                .ok
        );
        let c = &r.cands[1];
        assert_eq!(
            sent(&fake),
            vec![back(a.len() + 1 + b.len()), format!("{a}.{c}")]
        );
    }

    #[tokio::test]
    async fn refused_typing_still_answers_candidates_with_denied() {
        let (mut k, fake, term, layout) = open().await;
        let r = k
            .stroke(
                &term,
                &target(Some("Answer the request first")),
                &swipe(&layout, "hello", 1),
            )
            .await;
        assert_eq!((r.err, r.typed), (Some(ResultErr::Denied), None));
        assert!(!r.cands.is_empty());
        assert!(sent(&fake).is_empty());
        // nothing was typed, so there is nothing to send
        let res = k
            .cmd(&term, &target(None), CmdOp::KbSend, None, Some(1500))
            .await;
        assert!(!res.ok);
    }

    #[tokio::test]
    async fn a_stroke_for_another_keyboard_is_stale() {
        let (mut k, _, term, layout) = open().await;
        let mut s = swipe(&layout, "hello", 1);
        s.kid = 99;
        assert_eq!(
            k.stroke(&term, &target(None), &s).await.err,
            Some(ResultErr::Stale)
        );
    }

    #[tokio::test]
    async fn send_needs_a_hold_then_starts_a_new_composition() {
        let (mut k, fake, term, layout) = open().await;
        let t = target(None);
        k.stroke(&term, &t, &swipe(&layout, "hello", 1)).await;
        sent(&fake);
        let short = k.cmd(&term, &t, CmdOp::KbSend, None, Some(400)).await;
        assert_eq!(short.err, Some(ResultErr::Denied));
        assert!(sent(&fake).is_empty());
        assert!(k.cmd(&term, &t, CmdOp::KbSend, None, Some(1200)).await.ok);
        assert_eq!(sent(&fake), vec!["\\r"]);
        let r = k.stroke(&term, &t, &swipe(&layout, "world", 2)).await;
        assert_eq!(
            sent(&fake),
            vec![r.cands[0].clone()],
            "no leading space after a send"
        );
    }

    #[tokio::test]
    async fn end_erases_only_when_asked_and_drops_the_state() {
        let (mut k, fake, term, layout) = open().await;
        let t = target(None);
        let r = k.stroke(&term, &t, &swipe(&layout, "hello", 1)).await;
        sent(&fake);
        assert!(
            k.cmd(&term, &t, CmdOp::KbEnd, Some(Arg::Uint(1)), None)
                .await
                .ok
        );
        assert_eq!(
            sent(&fake),
            vec!["\\x7f".repeat(r.cands[0].chars().count())]
        );
        assert!(k.sid_of(1).is_none());

        let (mut k, fake, term, layout) = open().await;
        k.stroke(&term, &t, &swipe(&layout, "hello", 1)).await;
        sent(&fake);
        assert!(
            k.cmd(&term, &t, CmdOp::KbEnd, Some(Arg::Uint(0)), None)
                .await
                .ok
        );
        assert!(sent(&fake).is_empty());
    }

    #[tokio::test]
    async fn a_repeated_start_keeps_the_composition() {
        let (mut k, fake, term, layout) = open().await;
        let t = target(None);
        k.stroke(&term, &t, &swipe(&layout, "hello", 1)).await;
        sent(&fake);
        let again = KbStart {
            kid: 1,
            sid: 7,
            keys: layout.to_packed(),
        };
        assert!(k.start(&term, &t, &again).await);
        let r = k.stroke(&term, &t, &swipe(&layout, "world", 2)).await;
        assert_eq!(sent(&fake), vec![format!(" {}", r.cands[0])]);
    }

    #[tokio::test]
    async fn a_broken_layout_is_rejected() {
        let mut k = KeysLink::default();
        let term: Arc<dyn Terminal> = Arc::new(Fake::default());
        let bad = KbStart {
            kid: 1,
            sid: 7,
            keys: vec![1, 2, 3],
        };
        assert!(!k.start(&term, &target(None), &bad).await);
    }
}
