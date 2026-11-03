# Black Hand OS

Current Phase 2 code uses a **notcurses UI**, custom init and modem/audio/storage
services. Communication data is stored as JSON files, not SQLite. Earlier
LVGL and voice-assistant descriptions below are historical project goals.
See [remediation status](Phase-2/REMEDIATION-STATUS.md) for current architecture,
verified fixes and outstanding product/hardware work.

> **A multi-phase embedded operating system project demonstrating expertise from bare-metal RTOS to embedded Linux**

![Project Status](https://img.shields.io/badge/Phase%201-Complete-success)
![Project Status](https://img.shields.io/badge/Phase%202-In%20Progress-yellow)
![License](https://img.shields.io/badge/license-MIT-blue)

## 📋 Overview

Black Hand OS is an ambitious embedded systems project that progresses through two distinct phases, each targeting different aspects of embedded software development:

- **Phase 1**: Bare-metal RTOS development on STM32 microcontroller (FreeRTOS)
- **Phase 2**: Custom embedded Linux distribution for voice assistant device (Buildroot)

This project demonstrates end-to-end embedded systems expertise, from register-level hardware control to high-level system architecture, ML integration, and user interface design.

---

## 🎯 Project Goals

### Educational Objectives

- Master bare-metal embedded programming with direct register manipulation
- Understand real-time operating system concepts and implementation
- Learn embedded Linux internals (kernel configuration, init systems, device drivers)
- Integrate machine learning (speech-to-text) on resource-constrained hardware
- Build production-ready embedded software architecture

### Technical Outcomes

- Working RTOS-based system with cellular modem integration
- Custom Linux distribution with offline voice assistant capabilities
- Transferable skills across automotive, IoT, industrial, and consumer electronics domains

---

## 🏗️ Architecture

### Phase 1: STM32 + FreeRTOS (COMPLETED ✅)

```
┌─────────────────────────────────────────────────────┐
│                   FreeRTOS Kernel                   │
│            (Priority-based Scheduling)              │
└─────────────────────────────────────────────────────┘
         │              │              │
    ┌────▼───┐    ┌────▼───┐    ┌────▼────┐
    │ Audio  │    │ Modem  │    │   UI    │
    │  Task  │    │  Task  │    │  Task   │
    └────┬───┘    └────┬───┘    └────┬────┘
         │              │              │
    ┌────▼──────────────▼──────────────▼────┐
    │        Queues & Semaphores            │
    └───────────────────────────────────────┘
         │              │              │
    ┌────▼───┐    ┌────▼───┐    ┌────▼────┐
    │  ADC   │    │ UART   │    │  GPIO   │
    │  DMA   │    │  AT    │    │  I2C    │
    └────────┘    └────────┘    └─────────┘
         │              │              │
    ┌────▼──────────────▼──────────────▼────┐
    │          STM32F429 Hardware            │
    └───────────────────────────────────────┘
```

**Key Components**:

- **Bare-metal drivers**: UART, GPIO, I2C, SPI, ADC, DMA (direct register manipulation)
- **FreeRTOS**: Task scheduling, queues, semaphores, mutexes
- **Audio pipeline**: DMA-driven ADC sampling with circular buffering
- **Modem integration**: AT command parser for cellular communication

### Phase 2: Embedded Linux (IN PROGRESS 🔨)

```
┌─────────────────────────────────────────────────────┐
│              User Applications (LVGL UI)            │
│      ┌──────────┐  ┌─────────┐  ┌─────────┐       │
│      │  Voice   │  │  Notes  │  │Settings │       │
│      │ Recorder │  │   App   │  │   App   │       │
│      └─────┬────┘  └────┬────┘  └────┬────┘       │
└────────────┼────────────┼────────────┼─────────────┘
             │            │            │
┌────────────▼────────────▼────────────▼─────────────┐
│           Service Layer (IPC via Unix Sockets)     │
│  ┌─────────────┐  ┌──────────┐  ┌──────────────┐  │
│  │audio_service│  │stt_service│  │storage_service│ │
│  │   (ALSA)    │  │  (Vosk/   │  │   (SQLite)   │  │
│  │             │  │  Whisper) │  │              │  │
│  └─────────────┘  └──────────┘  └──────────────┘  │
└────────────────────────────────────────────────────┘
             │            │            │
┌────────────▼────────────▼────────────▼─────────────┐
│              Custom Init System (PID 1)            │
└────────────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────┐
│       Linux Kernel (Custom Configuration)          │
│    ┌──────────┐  ┌──────────┐  ┌──────────┐       │
│    │  ALSA    │  │Framebuffer│  │ Input   │        │
│    │ Drivers  │  │  Driver   │  │ Driver  │        │
│    └──────────┘  └──────────┘  └──────────┘        │
└────────────────────────────────────────────────────┘
             │
┌────────────▼────────────────────────────────────────┐
│            Hardware (Raspberry Pi / ARM SoC)       │
│  ┌─────────────┐  ┌────────────┐  ┌─────────────┐ │
│  │ USB Audio   │  │Touchscreen │  │  Display    │  │
│  │  Device     │  │            │  │             │  │
│  └─────────────┘  └────────────┘  └─────────────┘  │
└────────────────────────────────────────────────────┘
```

**Key Components**:

- **Custom Linux Distribution**: Buildroot-based minimal system
- **Custom Init**: PID 1 process managing all services
- **Service Architecture**: Modular design with IPC communication
- **Audio Pipeline**: ALSA-based capture/playback with USB audio support
- **Speech-to-Text**: Offline ML inference using Vosk or Whisper.cpp
- **UI Framework**: LVGL running on Linux framebuffer

---

## 🚀 Features

### Phase 1 Features (Completed)

- ✅ Real-time task scheduling with FreeRTOS
- ✅ Bare-metal hardware drivers (no HAL dependencies)
- ✅ DMA-driven audio capture with circular buffering
- ✅ AT command parser for cellular modem control
- ✅ Inter-task communication using queues and semaphores
- ✅ Priority-based preemptive multitasking
- ✅ Stack overflow detection and error handling
- ✅ UART debug logging with timestamps

### Phase 2 Features (In Progress)

- 🔨 Custom Buildroot Linux distribution
- 🔨 Minimal userspace with BusyBox
- 🔨 Custom init system (PID 1)
- 🔨 Service-oriented architecture with IPC
- 🔨 ALSA audio pipeline (USB audio I/O)
- 🔨 Offline speech-to-text engine integration
- 🔨 LVGL touchscreen user interface
- 🔨 Voice recorder application
- 🔨 Notes app with voice-to-text transcription
- ⏳ Storage service with SQLite backend
- ⏳ Settings and system management

### Phase 3 Features (Future)

- ⏳ Custom PCB design (NXP i.MX 8M Mini)
- ⏳ LTE modem integration for voice calls and SMS
- ⏳ Power management and battery optimization
- ⏳ Over-the-air (OTA) update mechanism

---

## 🛠️ Technology Stack

### Phase 1 Stack

| Component           | Technology                                 |
| ------------------- | ------------------------------------------ |
| **Microcontroller** | STM32F429I Discovery Board (ARM Cortex-M4) |
| **RTOS**            | FreeRTOS (v10.x)                           |
| **Toolchain**       | ARM GCC, OpenOCD                           |
| **IDE**             | VSCode with Cortex-Debug extension         |
| **Debugging**       | ST-Link, UART logging, GDB                 |
| **Peripherals**     | UART, GPIO, I2C, SPI, ADC, DMA, TIM        |

### Phase 2 Stack

| Component          | Technology                                  |
| ------------------ | ------------------------------------------- |
| **Hardware**       | Raspberry Pi 4 (ARM Cortex-A72)             |
| **Build System**   | Buildroot                                   |
| **Kernel**         | Linux (mainline, custom configuration)      |
| **Init System**    | Custom (PID 1 in C)                         |
| **Audio**          | ALSA (Advanced Linux Sound Architecture)    |
| **Speech-to-Text** | Vosk or Whisper.cpp                         |
| **UI Framework**   | LVGL (Light and Versatile Graphics Library) |
| **Database**       | SQLite                                      |
| **IPC**            | Unix domain sockets / D-Bus                 |
| **Languages**      | C, C++, Shell scripting                     |

---

## 📂 Project Structure

```
black-hand-os/
├── phase1/                      # STM32 + FreeRTOS implementation
│   ├── Core/
│   │   ├── Src/
│   │   │   ├── main.c          # FreeRTOS task initialization
│   │   │   ├── audio_task.c    # Audio capture task
│   │   │   ├── modem_task.c    # Modem communication task
│   │   │   └── ui_task.c       # User interface task
│   │   └── Inc/                # Header files
│   ├── Drivers/
│   │   ├── STM32F4xx_HAL_Driver/  # Minimal HAL usage
│   │   └── CMSIS/              # ARM CMSIS headers
│   ├── Middlewares/
│   │   └── FreeRTOS/           # FreeRTOS kernel source
│   └── docs/
│       ├── architecture.md     # Phase 1 architecture docs
│       └── hardware_setup.md   # Hardware configuration guide
│
├── phase2/                      # Embedded Linux implementation
│   ├── buildroot/              # Buildroot configuration
│   │   ├── configs/
│   │   │   └── blackhand_defconfig
│   │   └── board/
│   │       └── blackhand/
│   │           ├── rootfs_overlay/
│   │           └── post-build.sh
│   ├── init/
│   │   └── init.c              # Custom PID 1 init system
│   ├── services/
│   │   ├── audio_service/      # ALSA audio capture/playback
│   │   ├── stt_service/        # Speech-to-text engine
│   │   ├── storage_service/    # SQLite database management
│   │   └── ui_service/         # UI event handling
│   ├── apps/
│   │   ├── voice_recorder/     # Voice recorder application
│   │   ├── notes/              # Notes app with STT
│   │   └── settings/           # System settings
│   ├── ui/
│   │   └── lvgl_ui/            # LVGL UI implementation
│   └── docs/
│       ├── build_guide.md      # How to build the system
│       ├── service_api.md      # IPC API documentation
│       └── performance.md      # Performance benchmarks
│
├── docs/
│   ├── README.md               # This file
│   ├── PHASES.md               # Detailed phase roadmap
│   ├── ARCHITECTURE.md         # System architecture overview
│   └── CONTRIBUTING.md         # Contribution guidelines
│
├── tools/                       # Development tools and scripts
│   ├── flash_stm32.sh          # STM32 flashing script
│   ├── build_linux.sh          # Buildroot build script
│   └── test_audio.py           # Audio testing utilities
│
└── LICENSE
```

---

## 🎓 Learning Roadmap

This project follows a structured learning approach with 8 levels per phase. Each level builds on the previous, ensuring solid fundamentals before advancing.

### Phase 1 Levels (Completed)

1. ✅ **Boot & Control** - Reset vectors, interrupts, GPIO, UART logging
2. ✅ **RTOS Core** - Task management, scheduling, timing, memory
3. ✅ **Communication & Safety** - Queues, semaphores, mutexes, ISR rules
4. ✅ **Peripherals** - I2C, SPI, ADC, timers
5. ✅ **DMA** - Direct memory access for efficient data transfer
6. ✅ **Audio Processing** - Real-time audio capture and processing
7. ✅ **Modem Integration** - AT command parser, state machines
8. ✅ **Power & Production** - Power optimization, error recovery

### Phase 2 Levels (In Progress)

1. 🔨 **Linux Foundations** - Boot process, Buildroot, cross-compilation
2. 🔨 **Custom Init & Services** - PID 1, service architecture, IPC
3. ⏳ **Audio Pipeline** - ALSA configuration, capture/playback
4. ⏳ **Speech-to-Text** - ML model integration, optimization
5. ⏳ **UI Framework** - LVGL setup, screen design, touch input
6. ⏳ **Phone Features** - Voice recorder, notes, storage
7. ⏳ **System Integration** - Boot optimization, error handling
8. ⏳ **Polish & Validation** - Testing, documentation, demo

For detailed level breakdown, see [PHASES.md](docs/PHASES.md).

---

## 🔧 Getting Started

### Prerequisites

**Phase 1 (STM32 Development)**:

- STM32F429I Discovery Board
- ST-Link debugger (included with Discovery board)
- ARM GCC toolchain
- OpenOCD or STM32CubeProgrammer
- VSCode with Cortex-Debug extension (recommended)

**Phase 2 (Linux Development)**:

- Raspberry Pi 4 (4GB+ RAM recommended)
- MicroSD card (32GB+ recommended)
- USB audio device (or use built-in audio)
- Touchscreen display (optional, can use HDMI + mouse)
- Linux development machine (Ubuntu 20.04+ or similar)

### Building Phase 1 (STM32)

```bash
# Clone the repository
git clone https://github.com/yourusername/black-hand-os.git
cd black-hand-os/phase1

# Build using Make or CMake
make clean
make all

# Flash to STM32
./tools/flash_stm32.sh

# Monitor UART output
screen /dev/ttyUSB0 115200
```

### Building Phase 2 (Linux)

```bash
cd black-hand-os/phase2

# Install Buildroot dependencies (Ubuntu/Debian)
sudo apt-get install -y \
    build-essential git libncurses5-dev \
    bc wget cpio python3 unzip rsync

# Download and configure Buildroot
git clone https://github.com/buildroot/buildroot.git
cd buildroot
make BR2_EXTERNAL=../buildroot blackhand_defconfig

# Build the system (this takes 1-2 hours)
make

# Write to SD card (replace /dev/sdX with your SD card)
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress
sync

# Boot Raspberry Pi with the new image
# Connect via UART or SSH to monitor boot
```

For detailed build instructions, see [phase2/docs/build_guide.md](phase2/docs/build_guide.md).

---

## 📊 Current Status

### Phase 1: Complete ✅

- All 8 levels completed
- Working FreeRTOS system with audio and modem integration
- Documented and tested on STM32F429I

### Phase 2: Level 1-2 In Progress 🔨

- [x] Raspberry Pi booting stock Linux (hardware validation)
- [x] Buildroot environment configured
- [ ] Custom minimal Linux image building
- [ ] Custom init system implemented
- [ ] Service architecture designed
- [ ] ALSA audio pipeline working
- [ ] STT engine integrated
- [ ] UI framework operational
- [ ] Applications developed

**Estimated Completion**: April-May 2025 (working full-time)

---

## 🧪 Testing

### Phase 1 Testing

- Unit tests for individual drivers (UART, I2C, SPI)
- Integration tests for FreeRTOS task communication
- Stress tests for audio pipeline (continuous operation)
- Memory leak detection using FreeRTOS heap tracking
- Stack overflow detection with watermark analysis

### Phase 2 Testing (Planned)

- Boot time benchmarking (target: <5 seconds)
- Audio latency measurement
- STT accuracy testing (word error rate)
- Memory profiling with valgrind
- Service restart and error recovery testing
- 24-hour stability test

---

## 📈 Performance Targets

### Phase 1 Achieved Metrics

- **Boot time**: <100ms to main()
- **Task switch latency**: <10µs
- **Audio sampling**: 16kHz, 16-bit, real-time DMA transfer
- **Modem response time**: <100ms for AT commands
- **UART logging**: 115200 baud, no dropped messages

### Phase 2 Target Metrics

- **Boot time**: <5 seconds (kernel + services + UI)
- **Audio latency**: <50ms end-to-end
- **STT processing**: <2x real-time (10s audio → <20s processing)
- **Memory usage**: <512MB peak (including STT model)
- **UI responsiveness**: <50ms touch response time
- **Uptime**: 24+ hours without crashes or memory leaks

---

## 🤝 Contributing

This is primarily an educational project, but contributions, suggestions, and feedback are welcome!

### How to Contribute

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Contribution Guidelines

- Follow existing code style (see `.clang-format` for C/C++ style)
- Add comments for complex logic
- Update documentation for new features
- Test thoroughly before submitting PR

---

## 📚 Resources & References

### FreeRTOS Resources

- [FreeRTOS Official Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [Mastering the FreeRTOS Real Time Kernel](https://www.freertos.org/fr-content-src/uploads/2018/07/161204_Mastering_the_FreeRTOS_Real_Time_Kernel-A_Hands-On_Tutorial_Guide.pdf)
- [STM32 FreeRTOS Tutorial](https://www.digikey.com/en/maker/projects/getting-started-with-stm32-introduction-to-freertos/ad275395687e4d85935351e16ec575b1)

### Embedded Linux Resources

- [Buildroot User Manual](https://buildroot.org/downloads/manual/manual.html)
- [Embedded Linux Primer](https://www.amazon.com/Embedded-Linux-Primer-Practical-Real-World/dp/0137017839)
- [Linux Device Drivers Book](https://lwn.net/Kernel/LDD3/)
- [ALSA Documentation](https://www.alsa-project.org/wiki/Main_Page)

### Speech-to-Text Resources

- [Vosk Offline Speech Recognition](https://alphacephei.com/vosk/)
- [Whisper.cpp (C++ port of OpenAI Whisper)](https://github.com/ggerganov/whisper.cpp)

### LVGL Resources

- [LVGL Official Documentation](https://docs.lvgl.io/)
- [LVGL Examples](https://github.com/lvgl/lvgl/tree/master/examples)

---

## 🗺️ Roadmap

### 2025 Q1 (Current)

- ✅ Complete Phase 1
- 🔨 Begin Phase 2 (Levels 1-2)

### 2025 Q2

- ⏳ Complete Phase 2 (Levels 3-8)
- ⏳ Full system integration and testing
- ⏳ Documentation and demo video

### 2025 Q3+ (Optional Phase 3)

- ⏳ Custom PCB design (KiCad)
- ⏳ Component selection and BOM
- ⏳ PCB manufacturing and assembly
- ⏳ Hardware bring-up and debugging
- ⏳ LTE modem integration
- ⏳ Production-ready prototype

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## 👤 Author

**Mohammad** - Embedded Systems Engineer

- GitHub: [@yourusername](https://github.com/MRNOOR1)
- LinkedIn: [Your LinkedIn](www.linkedin.com/in/mr-noori)
- Email: themohammadrasoulnoori@gmail.com

---

## 🙏 Acknowledgments

- **FreeRTOS Community** for excellent RTOS documentation and examples
- **Buildroot Project** for making embedded Linux accessible
- **LVGL Team** for lightweight graphics library
- **Vosk/Whisper Teams** for open-source speech recognition
- **STMicroelectronics** for comprehensive STM32 documentation
- **Various online communities** (StackOverflow, Reddit r/embedded, Discord servers)

---

## 📝 Project Notes

This project demonstrates:

- **Bare-metal programming** without high-level abstractions
- **Real-time operating systems** with FreeRTOS
- **Embedded Linux** customization and optimization
- **Machine learning integration** on embedded devices
- **Full-stack embedded development** from hardware to UI
- **Production-ready practices** (error handling, logging, testing)

The goal is not just to build a device, but to **understand embedded systems deeply** from first principles, making this knowledge transferable to any embedded domain (automotive, IoT, industrial, consumer electronics).


