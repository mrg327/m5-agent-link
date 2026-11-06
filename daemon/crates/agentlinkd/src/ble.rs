//! BLE GATT transport (PROTOCOL §1: rx/tx characteristics, 1-byte fragment header).

use crate::transport::{Link, Transport};
use async_trait::async_trait;

// TODO: btleplug central; reassemble [FIRST|LAST|seq:6] fragments into whole envelopes.
pub struct BleTransport;

#[async_trait]
impl Transport for BleTransport {
    fn name(&self) -> &'static str {
        "ble"
    }

    async fn accept(&mut self) -> anyhow::Result<Box<dyn Link>> {
        anyhow::bail!("BLE transport not implemented yet")
    }
}
