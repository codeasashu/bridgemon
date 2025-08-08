# Asterisk AudioFork and BridgeMon Modules

This repository contains two Asterisk modules:

1. **AudioFork** - Forks raw audio streams to WebSocket servers
2. **BridgeMon** - Monitors bridge events and sets BRIDGEPEERID channel variable

## AudioFork Module

The AudioFork module allows you to fork raw audio streams from Asterisk channels to WebSocket servers. This is useful for real-time audio processing, recording, or streaming applications.

### Features

- Forks raw audio to WebSocket servers
- Supports TLS/SSL connections
- Configurable reconnection attempts and timeouts
- Volume adjustment options
- Direction control (in, out, both)
- Periodic beep functionality

### Usage

```asterisk
AudioFork(wsserver[,options])
```

### Options

- `b` - Only save audio while channel is bridged
- `B` - Play periodic beep (with optional interval)
- `v` - Adjust heard volume (-4 to 4)
- `V` - Adjust spoken volume (-4 to 4)
- `W` - Adjust both volumes (-4 to 4)
- `i` - Store AudioFork ID in channel variable
- `D` - Direction (in, out, both)
- `T` - TLS configuration
- `R` - Reconnection timeout
- `r` - Reconnection attempts

### Examples

```asterisk
; Basic usage
AudioFork(ws://localhost:8080/audio)

; With TLS
AudioFork(wss://localhost:8080/audio,T(cert.pem))

; With volume adjustment
AudioFork(ws://localhost:8080/audio,v(2),V(-1))
```

## BridgeMon Module

The BridgeMon module monitors bridge events and automatically sets the `BRIDGEPEERID` channel variable with the unique ID of the linked channel when a bridge join event occurs. This provides an O(1) way to get the linked channel's ID instead of using `BRIDGEPEER` and then querying the channel list API which is O(n).

### Problem Statement

Your Asterisk stasis app depends on finding the linked channels for a given channel. It uses `BRIDGEPEER` channel variable to get the linked channel's name, then has to use channel list API and filter the result with channel's name to get the channel id. This is an O(n) operation.

### Solution

BridgeMon provides an O(1) solution by:
1. Taking a channel's ID as input
2. Starting monitoring of the input channel's bridge using bridge hooks
3. Using `AST_BRIDGE_HOOK_TYPE_JOIN` to get callbacks when channels join bridges
4. Setting the `BRIDGEPEERID` channel variable with the linked channel's unique ID

### Features

- Monitors bridge join events
- Automatically sets `BRIDGEPEERID` channel variable
- O(1) lookup for linked channel IDs
- CLI and Manager API support
- Clean resource management

### Usage

#### Dialplan Application

```asterisk
; Start monitoring bridge events for current channel
BridgeMon(${CHANNEL(uniqueid)})

; Stop monitoring bridge events
StopBridgeMon()
```

#### CLI Commands

```bash
# Start monitoring
asterisk -rx "bridgemon start SIP/1001-00000001"

# Stop monitoring
asterisk -rx "bridgemon stop SIP/1001-00000001"
```

#### Manager API

```bash
# Start monitoring
Action: BridgeMon
Channel: SIP/1001-00000001
ChannelID: 1234567890.1

# Stop monitoring
Action: StopBridgeMon
Channel: SIP/1001-00000001
ChannelID: 1234567890.1
```

### Channel Variables

- `BRIDGEPEERID` - Set to the unique ID of the linked channel when a bridge join event occurs

## Installation

### Prerequisites

- Asterisk 16+ (tested with Asterisk 18)
- GCC compiler
- Make

### Build and Install

```bash
# Clone the repository
git clone <repository-url>
cd asterisk-audiofork

# Build the modules
make

# Install the modules
sudo make install

# Install sample configuration (optional)
sudo make samples
```

### Loading the Modules

Add the following lines to your `modules.conf`:

```ini
[modules]
autoload=yes

; Load the modules
load => app_audiofork.so
load => app_bridgemon.so
```

## Configuration

### AudioFork Configuration

Create `/etc/asterisk/audiofork.conf`:

```ini
[general]
; WebSocket server settings
wsserver = ws://localhost:8080/audio

; TLS settings (optional)
tls_cert = /path/to/cert.pem
tls_key = /path/to/key.pem

; Reconnection settings
reconnection_timeout = 5
reconnection_attempts = 3
```

## Examples

### AudioFork Examples

```asterisk
; Basic audio forking
exten => 1000,1,Answer()
exten => 1000,2,AudioFork(ws://localhost:8080/audio)
exten => 1000,3,Dial(SIP/1001)

; With volume adjustment and TLS
exten => 1001,1,Answer()
exten => 1001,2,AudioFork(wss://localhost:8080/audio,T(cert.pem),v(2),V(-1))
exten => 1001,3,Dial(SIP/1002)
```

### BridgeMon Examples

```asterisk
; Monitor bridge events and set BRIDGEPEERID
exten => 2000,1,Answer()
exten => 2000,2,BridgeMon(${CHANNEL(uniqueid)})
exten => 2000,3,Dial(SIP/2001)
exten => 2000,4,NoOp(BRIDGEPEERID=${BRIDGEPEERID})
exten => 2000,5,StopBridgeMon()
```

## Troubleshooting

### Common Issues

1. **Module not loading**: Check that Asterisk version is compatible and all dependencies are installed
2. **WebSocket connection failed**: Verify WebSocket server is running and accessible
3. **TLS connection issues**: Check certificate paths and permissions
4. **Bridge monitoring not working**: Ensure channel is actually joining a bridge

### Debugging

Enable verbose logging in `asterisk.conf`:

```ini
[options]
verbose = 5
debug = 5
```

Check logs for BridgeMon and AudioFork messages:

```bash
tail -f /var/log/asterisk/full | grep -E "(BridgeMon|AudioFork)"
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is licensed under the GNU General Public License Version 2. See the LICENSE file for details.

## Support

For issues and questions:
- Check the troubleshooting section above
- Review Asterisk documentation
- Open an issue on the repository