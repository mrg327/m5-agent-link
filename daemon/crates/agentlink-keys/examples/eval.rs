use agentlink_keys::eval::{eval_strokes, eval_taps, Report};
use agentlink_keys::synth::SynthParams;
use agentlink_keys::{dev_words, Decoder, Layout, Lexicon};
use std::time::Instant;

fn show(name: &str, r: &Report) {
    println!(
        "{name:<28} n={:<5} top1 {:5.1}%  top4 {:5.1}%  mean {:.2} ms  max {:.2} ms",
        r.n,
        r.top1_pct(),
        r.top4_pct(),
        r.mean_ms,
        r.max_ms
    );
}

fn main() {
    let lex = Lexicon::english();
    let common: Vec<String> = lex.top_words(2000).iter().map(|w| w.to_string()).collect();
    let common: Vec<&str> = common.iter().map(String::as_str).collect();
    let dev = dev_words();
    println!("lexicon: {} words", lex.len());
    let t0 = Instant::now();
    let dec = Decoder::new(Layout::stopwatch_qwerty(), lex);
    println!(
        "templates built in {:.0} ms",
        t0.elapsed().as_secs_f64() * 1e3
    );

    let sp = SynthParams::default();
    let strokes = eval_strokes(&dec, &common, 1, &sp);
    show("strokes, 2000 common", &strokes);
    show(
        "strokes, developer words",
        &eval_strokes(&dec, &dev, 1, &sp),
    );
    let rough = SynthParams {
        key_sigma: 0.35,
        jitter: 5.0,
        ..SynthParams::default()
    };
    show(
        "strokes, 2000 common, rough",
        &eval_strokes(&dec, &common, 1, &rough),
    );
    let taps = eval_taps(&dec, &common, 1, &sp);
    show("taps, 2000 common", &taps);
    show("taps, developer words", &eval_taps(&dec, &dev, 1, &sp));

    let swaps = |r: &Report| {
        r.worst_swaps(12)
            .iter()
            .map(|((a, b), n)| format!("{a}>{b}:{n}"))
            .collect::<Vec<_>>()
            .join(" ")
    };
    println!(
        "\nletter mix-ups in stroke top-1 misses (wanted>got:count): {}",
        swaps(&strokes)
    );
    println!("letter mix-ups in tap top-1 misses: {}", swaps(&taps));
}
