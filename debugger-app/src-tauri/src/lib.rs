// Serial capture of LucasLib debugger traces, straight from the V5 brain over
// USB — reading bytes in Rust (no GUI-terminal dropping). See debugger.cpp on
// the robot side; lines arrive marker-prefixed (~LLT|) and a trace runs from a
// "#LUCASLIB_TRACE" line to a "#END" line.

use std::io::Read;
use std::time::{Duration, Instant};
use tauri::ipc::Channel;

const MARK: &str = "~LLT|";

/// usbmodem serial ports, cu.* preferred over tty.*.
fn usbmodem_ports() -> Vec<String> {
    let mut ports: Vec<String> = serialport::available_ports()
        .map(|ps| {
            ps.into_iter()
                .map(|p| p.port_name)
                .filter(|n| n.contains("usbmodem"))
                .collect()
        })
        .unwrap_or_default();
    ports.sort_by_key(|p| if p.contains("cu.") { 0 } else { 1 });
    ports
}

#[tauri::command]
fn list_serial_ports() -> Vec<String> {
    usbmodem_ports()
}

/// Scan the V5's usbmodem ports and read from whichever is free (the VEX
/// extension holds the protocol port; the user-output port is separate), until
/// one complete trace arrives — returned as clean, marker-stripped text.
/// Streams progress over `on_status`. Re-scans across the USB re-enumeration
/// that happens when the robot program (re)starts.
#[tauri::command]
async fn capture_trace(
    timeout_secs: u64,
    on_status: Channel<String>,
) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || read_trace(timeout_secs, &on_status))
        .await
        .map_err(|e| e.to_string())?
}

fn read_trace(timeout_secs: u64, status: &Channel<String>) -> Result<String, String> {
    let deadline = Instant::now() + Duration::from_secs(timeout_secs);
    let mut lines: Vec<String> = Vec::new();
    let mut in_trace = false;
    let mut pending = String::new();
    let mut buf = [0u8; 1024];

    let mut ever_opened = false;
    let mut total_bytes: u64 = 0;
    let mut total_lines: u64 = 0;

    let _ = status.send("Scanning for the V5…".into());

    'reopen: while Instant::now() < deadline {
        let ports = usbmodem_ports();
        if ports.is_empty() {
            let _ = status.send("No V5 serial port found — plug in the brain.".into());
            std::thread::sleep(Duration::from_millis(400));
            continue;
        }
        // open the first port that isn't already held by another program
        let mut opened: Option<(String, Box<dyn serialport::SerialPort>)> = None;
        for p in &ports {
            if let Ok(s) = serialport::new(p, 115_200)
                .timeout(Duration::from_millis(300))
                .open()
            {
                opened = Some((p.clone(), s));
                break;
            }
        }
        let (name, mut sp) = match opened {
            Some(x) => {
                if !ever_opened {
                    ever_opened = true;
                    let _ =
                        status.send(format!("Reading {} — run your auton now…", x.0));
                }
                x
            }
            None => {
                let _ = status.send(format!(
                    "{} port(s) found but all busy — disconnect the V5 in the VEX extension.",
                    ports.len()
                ));
                std::thread::sleep(Duration::from_millis(400));
                continue;
            }
        };
        let _ = &name;

        loop {
            if Instant::now() >= deadline {
                break 'reopen;
            }
            match sp.read(&mut buf) {
                Ok(0) => break, // closed
                Ok(n) => {
                    total_bytes += n as u64;
                    for &b in &buf[..n] {
                        let c = b as char;
                        if c == '\n' {
                            if let Some(i) = pending.find(MARK) {
                                let content = pending[i + MARK.len()..].trim_end().to_string();
                                total_lines += 1;
                                if content.starts_with("#LUCASLIB_TRACE") {
                                    lines.clear();
                                    lines.push(content);
                                    in_trace = true;
                                    let _ = status.send("Trace started — receiving…".into());
                                } else if in_trace && content.starts_with("#END") {
                                    let _ = status
                                        .send(format!("Got it — {} lines.", lines.len()));
                                    return Ok(lines.join("\n"));
                                } else if in_trace {
                                    lines.push(content);
                                    if lines.len() % 50 == 0 {
                                        let _ =
                                            status.send(format!("Receiving… {} lines", lines.len()));
                                    }
                                }
                            }
                            pending.clear();
                        } else if c != '\r' {
                            pending.push(c);
                            if pending.len() > 8192 {
                                pending.clear(); // runaway line w/ no newline — resync
                            }
                        }
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => { /* no data yet */ }
                Err(_) => break, // disconnect → reopen
            }
        }
        std::thread::sleep(Duration::from_millis(150));
    }

    // Timed out — report the failure mode so it's debuggable.
    if !ever_opened {
        Err("Every usbmodem port was busy. Disconnect the V5 in the VEX extension (the device, not just the terminals) so a port frees up, then retry.".into())
    } else if total_bytes == 0 {
        Err("Connected, but no data arrived. Make sure the auton calls debugger::stop() (NOT stop(\"trace.txt\"), which writes to SD) and actually ran.".into())
    } else {
        Err(format!(
            "Saw {total_bytes} bytes / {total_lines} marker lines but no complete trace. Rebuilt + downloaded the latest robot code?"
        ))
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![list_serial_ports, capture_trace])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
