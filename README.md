# BlackHand OS

A learning-focused embedded operating system project for the STM32F429I-Discovery board. This project follows a systematic approach to learning embedded systems development, starting from bare-metal programming and progressing through RTOS concepts to embedded Linux.

## 🎯 Project Goals

**Primary Objectives:**
- Learn C programming in an embedded context
- Understand bare-metal microcontroller programming
- Master Real-Time Operating System (RTOS) concepts
- Build a foundation for embedded Linux development
- Develop professional embedded systems skills through hands-on implementation

**Learning Philosophy:**
- Start from fundamentals (no jumping ahead)
- Understand concepts before moving to next level
- Learn by implementing, not just copying code
- Build incrementally with working milestones at each step

## 🔧 Hardware Requirements

- **Board:** STM32F429I-Discovery
- **MCU:** STM32F429ZIT6 (ARM Cortex-M4F @ 180 MHz max, currently running @ 72 MHz)
- **Memory:** 256KB SRAM, 2MB Flash
- **Peripherals Used:**
  - UART1 (USART1) - Serial communication via ST-Link VCP
  - GPIO - LED control (PG13 Green, PG14 Red)
  - TIM2 - Hardware timer for precise timing
  - TIM6 - SysTick timebase

## 📚 Project Phases

### **Phase 1: Bare-Metal & RTOS (STM32F429I-Discovery)**
**Current Phase - In Progress**

1. ✅ **Level 1 - Task 1.1:** Reliable Boot (Bare-metal LED blink with polling)
2. ✅ **Level 1 - Task 1.2:** Timer Interrupt LED Blink (Interrupt-driven design)
3. ⏳ **Level 1 - Task 1.3:** Button with Debounce (External interrupts)
4. ⏳ **Level 1 - Task 1.4:** UART Debug Logging (Professional logging system)
5. ⏳ **Level 2:** FreeRTOS Integration
6. ⏳ **Level 3:** Inter-task Communication
7. ⏳ **Level 4:** DMA & Timers
8. ⏳ **Level 5:** Audio Playback
9. ⏳ **Level 6:** LVGL UI
10. ⏳ **Level 7:** Code Structure
11. ⏳ **Level 8:** Pushing Limits

### **Phase 2: Embedded Linux (Raspberry Pi / Custom Board)**
**Future Phase**

Details in [Phases.md](Phases.md)

## 🚀 Current Implementation (Task 1.2)

### Features
- **Hardware Timer Interrupt:** TIM2 configured for precise 1 Hz timing
- **LED Blink:** Green LED (PG13) toggles every 1000ms via timer interrupt
- **UART Output:** Debug messages via printf redirected to UART1 (115200 baud)
- **Interrupt-Driven Architecture:** CPU idle between timer events
- **NVIC Configuration:** Priority-based interrupt handling

### Technical Details
- **System Clock:** 72 MHz (PLL from 8 MHz HSE)
- **Timer Prescaler:** 7199 (divides 72 MHz to 10 kHz)
- **Timer Period:** 9999 (counts to 10,000)
- **Update Frequency:** 10 kHz ÷ 10,000 = 1 Hz (1 second)
- **Interrupt Priority:** TIM2_IRQn = 0 (highest)

## 🛠️ Build Instructions

### Prerequisites
- **Toolchain:** ARM GCC compiler (`arm-none-eabi-gcc`)
- **Build System:** GNU Make
- **Programmer:** STM32CubeProgrammer (CLI or GUI)
- **Terminal:** PuTTY, TeraTerm, or similar for serial monitoring

### Building the Project

```bash
# Clean previous build
make clean

# Build the project
make

# Expected output:
# text    data     bss     dec     hex filename
# 32112    108   90696  122916   1e044 build/BlackHand.elf
```

### Flashing to Board

**Option 1: Command Line (if STM32_Programmer_CLI is in PATH)**
```bash
make flash
```

**Option 2: STM32CubeProgrammer GUI**
1. Open STM32CubeProgrammer
2. Connect to ST-Link
3. Select `build/BlackHand.elf`
4. Click "Download"

### Viewing Serial Output

1. **Connect:** USB cable to ST-Link port
2. **Terminal Settings:**
   - Port: COM3 (check Device Manager)
   - Baud Rate: 115200
   - Data Bits: 8
   - Stop Bits: 1
   - Parity: None
   - Flow Control: None
3. **Press RESET** button on board
4. **Expected Output:**
   ```
   ========================================
      STM32F429I-Discovery Boot
      Phase 1 - Level 1: Boot & Control
   ========================================
   System Clock: 72 MHz
   Starting bare-metal LED blink...

   TIM2 started
   [TIM2 ISR] LED Toggle #1
   [TIM2 ISR] LED Toggle #2
   [TIM2 ISR] LED Toggle #3
   ...
   ```

## 📁 Project Structure

```
BlackHand/
├── Core/
│   ├── Inc/              # Header files
│   │   ├── main.h
│   │   ├── tim.h
│   │   ├── usart.h
│   │   └── ...
│   ├── Src/              # Source files
│   │   ├── main.c        # Main application
│   │   ├── tim.c         # Timer configuration
│   │   ├── usart.c       # UART configuration
│   │   ├── stm32f4xx_it.c # Interrupt handlers
│   │   └── ...
│   └── Startup/          # Startup code
├── Drivers/              # HAL and CMSIS drivers
├── Middlewares/          # FreeRTOS (disabled for Level 1)
├── build/                # Compiled binaries
├── Makefile              # Build configuration
├── Phases.md             # Detailed roadmap
├── PROGRESS.md           # Task tracking
└── README.md             # This file
```

## 📖 Key Learning Concepts (Task 1.2)

### What You'll Learn
1. **Hardware Timers**
   - Prescaler and period configuration
   - Auto-reload mode
   - Timer clock sources

2. **Interrupts (NVIC)**
   - Interrupt Service Routines (ISR)
   - Interrupt priorities
   - Enabling/disabling interrupts

3. **HAL Callback Pattern**
   - Weak function overriding
   - `HAL_TIM_PeriodElapsedCallback()`
   - Callback vs direct ISR implementation

4. **C Programming Concepts**
   - Static variables in functions
   - Struct member access
   - Function pointers (implicit in callbacks)
   - Header/source file organization

5. **Embedded Design Patterns**
   - Polling vs Interrupt-driven
   - Event-driven architecture
   - Power efficiency considerations

## 🔍 Troubleshooting

### Build Fails
- Verify ARM GCC toolchain is installed: `arm-none-eabi-gcc --version`
- Check Makefile paths are correct
- Run `make clean` before rebuilding

### Flash Fails
- Check ST-Link drivers are installed
- Verify board is connected via USB
- Try flashing with STM32CubeProgrammer GUI
- Press RESET button before flashing

### No Serial Output
- Verify COM port number in Device Manager
- Check terminal baud rate is 115200
- Ensure UART TX (PA9) is connected via ST-Link VCP
- Press RESET button after opening terminal

### LED Doesn't Blink
- Check if green LED (PG13) is soldered correctly
- Verify timer interrupt is enabled in code
- Check `HAL_TIM_Base_Start_IT()` is called
- Use debugger to check if ISR is being called

## 📝 Development Workflow

1. **Understand the Concept:** Read documentation, understand theory
2. **Implement Code:** Write code manually (don't copy/paste)
3. **Build:** Compile and fix any errors
4. **Flash:** Upload to board
5. **Test:** Verify behavior matches expectations
6. **Debug:** Use printf, LED indicators, or debugger
7. **Document:** Update PROGRESS.md and commit changes
8. **Move On:** Only proceed to next task when current works perfectly

## 🎓 Resources

- [STM32F429 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00031020.pdf)
- [STM32F429I-Discovery User Manual](https://www.st.com/resource/en/user_manual/dm00093903.pdf)
- [HAL Driver Documentation](https://www.st.com/resource/en/user_manual/dm00105879.pdf)
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ARM Cortex-M4 Programming Manual](https://developer.arm.com/documentation/dui0553/latest/)

## 🤝 Contributing

This is a personal learning project. Code is written manually to maximize learning, not optimized for production use.

## 📄 License

Copyright (c) 2025 STMicroelectronics (for HAL/BSP code)
Custom application code is for educational purposes.

## 🎯 Next Steps

See [PROGRESS.md](PROGRESS.md) for detailed task tracking and next objectives.

**Current Focus:** Task 1.2 - Test timer interrupt implementation on hardware

---

**Project Started:** January 2025
**Current Status:** Phase 1, Level 1, Task 1.2 (Timer Interrupts)
**Learning Approach:** Systematic, ground-up embedded systems development
