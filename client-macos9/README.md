# Mochila - Mac OS 9.2.2 Carbon Client

Classic Mac OS Carbon/QuickDraw client for Mochila, designed to compile under CodeWarrior Pro 7/8 on Mac OS 9.2.2.

## Source Files

**src/**
- `main_carbon.cpp` - Carbon application entry point and event loop
- `renderer_quickdraw.cpp` - QuickDraw offscreen GWorld double-buffering and native PICT rendering
- `ot_websocket.cpp` - OpenTransport TCP socket client with WebSocket framing
- `wire_protocol.cpp` - Binary wire protocol parser
- `primitive_store.cpp` - Primitive state manager
- `preferences.cpp` - Simple preferences system

**include/**
- `renderer_quickdraw.h`
- `ot_websocket.h`
- `wire_protocol.h`
- `primitive_store.h`
- `types.h`
- `preferences.h`

## Building with CodeWarrior Pro 8

### 1. Create a New Project

1. Open CodeWarrior IDE on your Mac OS 9.2.2 machine
2. Go to **File -> New Project...**
3. Select **Empty Project**
4. Name the project `MochilaCarbon.mcp` and save it to the `client-macos9` folder 

### 2. Add Source Files

Drag and drop these files into the Project window:
- `src/main_carbon.cpp`
- `src/renderer_quickdraw.cpp`
- `src/ot_websocket.cpp`
- `src/wire_protocol.cpp`
- `src/primitive_store.cpp`

### 3. Add Required Libraries

Add these libraries to your project (drag from CodeWarrior's `MacOS Support` folder):
- `MSL_All_Carbon.Lib` - Metrowerks Standard Library
- `Carbon.lib` - Carbon framework

### 4. Configure Target Settings

Press `Cmd+Shift+K` to open Target Settings:

**PPC Target:**
- Project: Application
- File Name: `MochilaCarbonApp.out`
- Creator: `DYLA`
- Type: `APPL`
- Preferred Heap Size (k): `16384` (adjust as needed; if your machine can handle a larger heap, you can increase this. 64MB is a good target for G3/G4 machines.)
- Minimum Heap Size (k): `8192`
- Stack Size (k): `256`
- Enable 'SIZE' Flags checkbox

**C/C++ Language:**
- Enable **ISO C++ Template Parser**
- Enable **C++ Exceptions**
- Enable **RTTI**
- Enable **bool Support**
- Enable **wchar_t Support**
- Enable **Bottom-up Inlining**
- Enable **Reuse Strings**
- Inline Depth: **Smart**
- Prefix File: `MacHeadersCarbon.h`

**Access Paths:**
User Paths:
- `{Project}:`
- `{YOUR_FOLDER} client-macos9:include:`
- `{YOUR_FOLDER} client-macos9:src:`

System Paths:
- `{Compiler}:`

### 5. Build and Run

Press `Cmd+R` to build and run.

## Connecting to Server

The client will connect to your server's IP address on port 8080.

Make sure the server is running first:
```bash
npm start
```

The client will connect to `ws://<your-server-ip>:8080` and start rendering web pages natively with QuickDraw.

## Requirements

- Mac OS 9.2.2 (For development I used 9.2.2 in a VM with UTM)
- CodeWarrior Pro 8 updated to 8.3
- PowerPC G3/G4 or equivalent recommended
