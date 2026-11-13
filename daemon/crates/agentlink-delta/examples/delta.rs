//! `cargo run --release -p agentlink-delta --example delta -- create BASE NEW PATCH` (or `apply BASE PATCH
//! NEW`): makes or applies a patch and says how long it took.
fn main() {
    let a: Vec<String> = std::env::args().skip(1).collect();
    let [op, x, y, z] = a.as_slice() else {
        eprintln!("usage: delta create BASE NEW PATCH | delta apply BASE PATCH NEW");
        std::process::exit(2);
    };
    let (x, y) = (std::fs::read(x).unwrap(), std::fs::read(y).unwrap());
    let t = std::time::Instant::now();
    let out = match op.as_str() {
        "create" => agentlink_delta::create_patch(&x, &y),
        "apply" => agentlink_delta::apply_patch(&x, &y).unwrap(),
        _ => panic!("create or apply"),
    };
    eprintln!(
        "{op}: {} bytes in {:.2} s",
        out.len(),
        t.elapsed().as_secs_f64()
    );
    std::fs::write(z, out).unwrap();
}
