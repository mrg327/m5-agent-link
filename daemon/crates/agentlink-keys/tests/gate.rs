use agentlink_keys::eval::{eval_strokes, eval_taps};
use agentlink_keys::synth::{synth_stroke, synth_taps, Rng, SynthParams};
use agentlink_keys::{pack_points, unpack_points, Context, Decoder, Layout, Lexicon, Point};

fn decoder() -> Decoder {
    Decoder::new(Layout::stopwatch_qwerty(), Lexicon::english())
}

fn stroke(dec: &Decoder, word: &str, seed: u64) -> Vec<Point> {
    let mut rng = Rng::new(seed);
    synth_stroke(dec.layout(), word, &mut rng, &SynthParams::default()).unwrap()
}

#[test]
fn accuracy_gate_on_common_words() {
    let lex = Lexicon::english();
    // every 7th of the top 2000: 286 words
    let words: Vec<String> = lex
        .top_words(2000)
        .iter()
        .step_by(7)
        .map(|w| w.to_string())
        .collect();
    let words: Vec<&str> = words.iter().map(String::as_str).collect();
    let dec = Decoder::new(Layout::stopwatch_qwerty(), lex);
    let sp = SynthParams::default();
    let r = eval_strokes(&dec, &words, 3, &sp);
    println!(
        "strokes: n={} top1 {:.1}% top4 {:.1}% mean {:.2} ms max {:.2} ms",
        r.n,
        r.top1_pct(),
        r.top4_pct(),
        r.mean_ms,
        r.max_ms
    );
    assert!(r.top4_pct() >= 90.0, "top4 {}", r.top4_pct());
    assert!(r.top1_pct() >= 70.0, "top1 {}", r.top1_pct());

    let t = eval_taps(&dec, &words, 3, &sp);
    assert!(t.top4_pct() >= 95.0, "tap top4 {}", t.top4_pct());
    assert!(t.top1_pct() >= 85.0, "tap top1 {}", t.top1_pct());
}

/// CPU time of the calling thread, so time-slicing with other processes does not count.
#[cfg(unix)]
fn thread_cpu_ms() -> f64 {
    let mut ts = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    // SAFETY: `ts` is a valid, writable timespec.
    let rc = unsafe { libc::clock_gettime(libc::CLOCK_THREAD_CPUTIME_ID, &mut ts) };
    assert_eq!(rc, 0, "clock_gettime");
    ts.tv_sec as f64 * 1000.0 + ts.tv_nsec as f64 / 1e6
}

#[cfg(not(unix))]
fn thread_cpu_ms() -> f64 {
    use std::sync::OnceLock;
    static T0: OnceLock<std::time::Instant> = OnceLock::new();
    T0.get_or_init(std::time::Instant::now)
        .elapsed()
        .as_secs_f64()
        * 1000.0
}

/// Kept apart from the accuracy gate, which is deterministic: wall-clock latency depends on
/// machine load (issue 20261111-5617), so this measures thread CPU time and takes the best of a
/// few runs per word to drop cache and frequency noise.
#[test]
fn stroke_latency_budget() {
    let lex = Lexicon::english();
    let words: Vec<String> = lex
        .top_words(2000)
        .iter()
        .step_by(7)
        .map(|w| w.to_string())
        .collect();
    let dec = Decoder::new(Layout::stopwatch_qwerty(), lex);
    let mut worst = (0.0f64, String::new());
    for (i, w) in words.iter().enumerate() {
        let mut rng = Rng::new(i as u64);
        let Some(s) = synth_stroke(dec.layout(), w, &mut rng, &SynthParams::default()) else {
            continue;
        };
        let best = (0..3)
            .map(|_| {
                let t0 = thread_cpu_ms();
                std::hint::black_box(dec.decode_stroke(&s, 4));
                thread_cpu_ms() - t0
            })
            .fold(f64::MAX, f64::min);
        if best > worst.0 {
            worst = (best, w.clone());
        }
    }
    println!("stroke latency: worst {:.2} ms CPU ({})", worst.0, worst.1);
    // unoptimised builds are an order of magnitude slower than the 20 ms release target
    assert!(worst.0 < 200.0, "{:.2} ms CPU on {:?}", worst.0, worst.1);
}

#[test]
fn context_words_win_and_identifiers_surface() {
    let mut dec = decoder();
    dec.set_context(&Context {
        text: vec!["Should I flash check_dram.py now?".into()],
        prev_word: None,
    });
    for seed in 0..5 {
        let c = dec.decode_stroke(&stroke(&dec, "flash", seed), 4);
        assert_eq!(c[0].word, "flash", "seed {seed}: {c:?}");
        let c = dec.decode_stroke(&stroke(&dec, "dram", seed), 4);
        assert!(
            c.iter()
                .any(|c| c.word == "dram" || c.word == "check_dram.py"),
            "seed {seed}: {c:?}"
        );
    }
    // the letter skeleton of the whole identifier gives it back with its case and punctuation
    let c = dec.decode_stroke(&stroke(&dec, "checkdrampy", 1), 4);
    assert!(c.iter().any(|c| c.word == "check_dram.py"), "{c:?}");
    // a part swiped alone offers the identifier as an expansion
    let c = dec.decode_stroke(&stroke(&dec, "check", 2), 4);
    assert!(c.iter().any(|c| c.word == "check_dram.py"), "{c:?}");
}

#[test]
fn camel_case_and_flags_keep_case() {
    let mut dec = decoder();
    dec.set_context(&Context {
        text: vec!["run ctest --output-on-failure in CamelCaseName".into()],
        prev_word: None,
    });
    let c = dec.decode_stroke(&stroke(&dec, "outputonfailure", 4), 4);
    assert!(c.iter().any(|c| c.word == "--output-on-failure"), "{c:?}");
    let c = dec.decode_stroke(&stroke(&dec, "camelcasename", 4), 4);
    assert!(c.iter().any(|c| c.word == "CamelCaseName"), "{c:?}");
}

#[test]
fn bigram_context_breaks_ties() {
    let mut dec = decoder();
    // "kind" and "mind" are neighbours on the keyboard
    dec.set_context(&Context {
        text: vec!["never mind the build".into()],
        prev_word: Some("never".into()),
    });
    let c = dec.decode_stroke(&stroke(&dec, "mind", 9), 4);
    assert_eq!(c[0].word, "mind", "{c:?}");
}

#[test]
fn tap_policy() {
    let dec = decoder();
    let mut rng = Rng::new(1);
    let sp = SynthParams {
        tap_sigma: 0.05,
        ..SynthParams::default()
    };
    // exact taps on a known word: literal first
    let taps = synth_taps(dec.layout(), "the", &mut rng, &sp).unwrap();
    assert_eq!(dec.decode_taps(&taps, 4)[0].word, "the");
    // exact taps on a non-word: a correction first, literal kept right behind
    let taps = synth_taps(dec.layout(), "thr", &mut rng, &sp).unwrap();
    let c = dec.decode_taps(&taps, 4);
    assert_eq!(c[1].word, "thr", "{c:?}");
    assert_ne!(c[0].word, "thr");
    // a single tap is one letter
    let taps = synth_taps(dec.layout(), "a", &mut rng, &sp).unwrap();
    assert_eq!(dec.decode_taps(&taps, 4)[0].word, "a");
    // a short wiggle is a tap too
    let wiggle = [
        Point {
            x: 73,
            y: 238,
            t_ms: 0,
        },
        Point {
            x: 78,
            y: 241,
            t_ms: 33,
        },
    ];
    assert!(Decoder::is_tap(&wiggle));
    assert_eq!(dec.decode_stroke(&wiggle, 4)[0].word, "a");
}

#[test]
fn packed_forms_round_trip() {
    let l = Layout::stopwatch_qwerty();
    let l2 = Layout::from_packed(&l.to_packed()).unwrap();
    assert_eq!(l.keys(), l2.keys());
    assert!(Layout::from_packed(&[0; 11]).is_err());
    assert_eq!(l.key_at(40, 180).unwrap().cp, 'q' as u32);
    assert_eq!(l.key_at(200, 350).unwrap().cp, 0x20);
    assert!(l.key_at(5, 5).is_none());
    let pts = [
        Point {
            x: -3,
            y: 466,
            t_ms: 700,
        },
        Point {
            x: 9,
            y: 10,
            t_ms: 1,
        },
    ];
    assert_eq!(unpack_points(&pack_points(&pts)).unwrap(), pts);
    assert!(unpack_points(&[0; 7]).is_err());
}

#[test]
fn custom_layout_works() {
    // two rows of five letters, nothing like QWERTY
    let keys = "abcdefghij"
        .chars()
        .enumerate()
        .map(|(i, c)| agentlink_keys::Key {
            cp: c as u32,
            x: 50 * (i % 5) as i16,
            y: 60 * (i / 5) as i16,
            w: 50,
            h: 60,
        })
        .collect();
    let lex = Lexicon::from_ranked(["bad", "fig", "jade", "hi", "ace"]);
    let dec = Decoder::new(Layout::new(keys), lex);
    let mut rng = Rng::new(2);
    let s = synth_stroke(dec.layout(), "jade", &mut rng, &SynthParams::default()).unwrap();
    assert_eq!(dec.decode_stroke(&s, 3)[0].word, "jade");
}
