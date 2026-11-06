//! USB CDC transport (PROTOCOL §1: `COBS(payload ‖ crc16)` + `0x00`).

use crate::transport::{Link, Transport};
use async_trait::async_trait;

// TODO: serialport; open without toggling DTR/RTS (ESP32-S3 would reset); COBS + CRC16 framing.
pub struct UsbTransport;

impl UsbTransport {
    pub fn new() -> Self {
        Self
    }
}

#[async_trait]
impl Transport for UsbTransport {
    fn name(&self) -> &'static str {
        "usb"
    }

    async fn accept(&mut self) -> anyhow::Result<Box<dyn Link>> {
        anyhow::bail!("USB transport not implemented yet")
    }
}
