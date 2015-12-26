# Beam FTL Protocol SDK

## Overview
FTL is Beam's next generation streaming protocol based on WebRTC to provide near-zero latency to game streams. It's designed to be an alternative to standard H.264/RTMP based streams as it requires special browser and client support to decode a stream. This SDK is designed to allow streamer platforms to integrate support for FTL into their software easily. This SDK is licensed under the terms of the MIT license, see COPYING for details.

At its heart, FTL uses the Secure Realtime Transmission Protocol (SRTP) to encode VP8 video data, and Opus audio data for decoding in the browser. Its designed to be relatively flexible and allow additional codecs to be integrated as they are adopted by browsers. As of writing, Firefox, Chrome, and Opera support WebRTC out-of-the-box. While Microsoft has stated that Edge will eventually gain some WebRTC support, this support has not materialized as of yet.

As SRTP is a stateless protocol by design, FTL incorporates a secondary helper protocol known as Charon to manage state information of a stream, and relay metadata to our ingest service known as Styx. Charon manages streamer authentication, SRTP key exchange, and video metadata such as resolution as well as a set of utilities to help measure and debug FTL related programs. Together, they allow a seamless experience of near-zero latency video.

## Terminology

FTL is made of up many different components which all interoperate to provide end-to-end functionality. This section is a quick cheat sheet of the components of our stack which may come up in conversations with our developers and support staff.

* FTL - Short for "Faster Than Light", FTL incorporates the transmission of video from ingest to dist, and finally to the browser via the UDP-based WebRTC protocol
* Charon - Handshake protocol for FTL. Named after the ferryman of the river Styx
* Janus - Short for the Meetecho Janus WebRTC gateway. Handles browser side intergration and distribution. https://janus.conf.meetecho.com/
* Plex - Stream manager plugin for Janus.
* Styx - Ingest daemon on Beam's side.
* Tarturus - Transcoding Services (not implemented as of writing)
* RTP - Realtime Transmission Protocol - A method of transmitting a single stream of audio or video over UDP point to point.
* SRTP - Secure RTP. An extension of RTP which implemented authetication and encryption
* SSRC - a unique 32-bit integer in a RTP packet which identifies a stream. Charon generates SSRC numbers for each RTP stream it ingests.
* SDP - Session Description Protocol. A handshake protocol used by WebRTC to establish a connection to Janus and Plex.

## Compilation
```
mkdir build
cd build
cmake ..
make
```

## Usage
`./charon -h INGEST_IP -c CHANNEL_ID -a CHANNEL_STREAM_KEY`
