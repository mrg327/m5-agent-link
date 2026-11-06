//! Every shared vector must decode, re-encode byte-identically, and match its symbolic JSON.

use agentlink_proto::Envelope;
use std::path::PathBuf;

fn vectors() -> Vec<(String, serde_json::Value)> {
    let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../protocol/vectors");
    let mut out: Vec<_> = std::fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("{}: {e}", dir.display()))
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|x| x == "json"))
        .map(|p| {
            let name = p.file_stem().unwrap().to_string_lossy().into_owned();
            (
                name,
                serde_json::from_slice(&std::fs::read(&p).unwrap()).unwrap(),
            )
        })
        .collect();
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

#[test]
fn all_vectors_round_trip() {
    let vs = vectors();
    assert!(vs.len() >= 12, "expected >= 12 vectors, found {}", vs.len());
    for (name, v) in vs {
        let bytes = hex::decode(v["cbor_hex"].as_str().unwrap()).unwrap();
        let env = Envelope::from_bytes(&bytes).unwrap_or_else(|e| panic!("{name}: decode: {e}"));
        let again = env.to_bytes().unwrap();
        assert_eq!(
            hex::encode(&again),
            hex::encode(&bytes),
            "{name}: re-encode differs"
        );
        assert_eq!(
            serde_json::to_value(&env).unwrap(),
            v["symbolic"],
            "{name}: symbolic JSON differs"
        );
    }
}

#[test]
fn unknown_type_and_keys_are_tolerated() {
    // {0: 99, 1: 1, 9: "x", 3: {}}
    let bytes = hex::decode("a4001863010109617803a0").unwrap();
    let env = Envelope::from_bytes(&bytes).unwrap();
    assert_eq!(env.msg.type_id(), 99);
    assert_eq!(env.id, 1);
}
