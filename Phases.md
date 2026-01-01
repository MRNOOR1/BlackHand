# 🎮 BLACK HAND OS - PHASE 1 COMPLETE ROADMAP

## How This Works:

- Each level has **LEARN** (knowledge) and **DO** (tasks)
- You cannot skip levels
- You unlock the next level only when ALL conditions are met
- Each level builds on previous levels

---

# 🟦 LEVEL 1: BOOT & CONTROL

**Theme:** "I own this hardware"

## 📚 LEARN (Knowledge Requirements)

### STM32 Boot Process

- [ ] **Reset vector** → what executes first
- [ ] **Startup code** → before main()
- [ ] **Clock configuration** → HSE, PLL, system clock
- [ ] **Memory map** → Flash, SRAM, peripherals

### Interrupts

- [ ] **NVIC** (Nested Vectored Interrupt Controller)
- [ ] **Priority levels** (0-15, lower number = higher priority)
- [ ] **ISR** (Interrupt Service Routine) rules
- [ ] **Pending vs active** interrupts

### GPIO

- [ ] **Input modes** (pull-up, pull-down, floating)
- [ ] **Output modes** (push-pull, open-drain)
- [ ] **Speed settings** and why they matter
- [ ] **Atomic operations** (read-modify-write problem)

### Debugging

- [ ] **UART** for logging
- [ ] **printf** redirection to UART
- [ ] **Breakpoints** in debugger
- [ ] **Register inspection**

---

## ✅ DO (Tasks to Complete)

### Task 1.1: Reliable Boot

```c
// You must achieve this:
- Board boots in <100ms every single time
- No random hangs
- No clock failures
- SystemClock_Config() works reliably
```

**Deliverable:**

- Boot sequence logged over UART
- System clock frequency printed (should be 180 MHz)

---

### Task 1.2: Timer Interrupt LED Blink

```c
// Requirements:
- Use TIM2 or similar
- 1 Hz blink (500ms on, 500ms off)
- NO delay loops in main
- LED toggled in timer ISR
```

**Deliverable:**

- LED blinks perfectly timed
- main() does nothing but wait
- UART logs each blink event

---

### Task 1.3: Button with Debounce

```c
// Requirements:
- External interrupt on button press (EXTI)
- Software debounce (20-50ms)
- No spurious triggers
- Count button presses
```

**Deliverable:**

- Press button 10 times → count = 10
- No double-counts
- UART logs each press with timestamp

---

### Task 1.4: UART Debug Logging

```c
// Requirements:
- printf() works over UART
- Baud rate: 115200
- Timestamp each log message
- Use HAL_GetTick() or similar
```

**Deliverable:**

```
[0000] System boot
[0012] LED ON
[0512] LED OFF
[1012] LED ON
[1234] Button pressed (count: 1)
```

---

## 🔓 UNLOCK CONDITIONS

You can move to Level 2 ONLY when you can answer these:

### Quiz Yourself:

1. **What happens between power-on and main()?**

   - (Reset handler, clock init, .data/.bss setup, jump to main)

2. **Why does the LED blink at exactly 1 Hz?**

   - (Timer configured for specific period, interrupt fires, toggles GPIO)

3. **What happens if you don't debounce the button?**

   - (Mechanical bounce causes multiple interrupts per press)

4. **Why use interrupts instead of polling?**

   - (CPU can do other work, more responsive, lower power)

5. **What is NVIC and why does priority matter?**

   - (Interrupt controller, prevents low-priority ISRs from blocking critical ones)

### Must Be Able to Draw:

```
Power On → Reset Handler → SystemInit() → Clock Config → main()
                                                            ↓
                                                    Infinite Loop
                                                            ↑
                                    Timer ISR ←──────────── TIM2 IRQ
                                    Button ISR ←─────────── EXTI IRQ
```

---

## ❌ STILL LOCKED:

- Tasks, queues, mutexes
- DMA
- Audio
- Multiple simultaneous operations

---

# 🟦 LEVEL 2: RTOS CORE

**Theme:** "I control time and priority"

## 📚 LEARN (Knowledge Requirements)

### FreeRTOS Fundamentals

- [ ] **Task** vs **thread** (conceptually same on RTOS)
- [ ] **Task states** (Running, Ready, Blocked, Suspended)
- [ ] **Scheduler** (preemptive, priority-based)
- [ ] **Context switch** (what it costs)

### Task Management

- [ ] **xTaskCreate()** parameters
- [ ] **Stack size** calculation
- [ ] **Priority** assignment rules
- [ ] **vTaskDelay()** vs **vTaskDelayUntil()**
- [ ] **Idle task** and hook functions

### Timing

- [ ] **Tick rate** (configTICK_RATE_HZ)
- [ ] **Tick interrupt** (SysTick)
- [ ] **Blocking vs busy-wait**
- [ ] **Starvation** (low priority task never runs)

### Memory

- [ ] **Heap** management (heap_4.c or heap_5.c)
- [ ] **Stack overflow** detection
- [ ] **configTOTAL_HEAP_SIZE**

---

## ✅ DO (Tasks to Complete)

### Task 2.1: Three Basic Tasks

```c
// Create these tasks:

Task A: LED Blink (Priority 1)
- Toggle LED every 500ms
- Use vTaskDelay(500)

Task B: Button Monitor (Priority 2)
- Check button state every 100ms
- Log when pressed

Task C: Heartbeat Logger (Priority 0)
- Print "alive" every 2 seconds
- Lowest priority
```

**Deliverable:**

- All three tasks run concurrently
- LED blinks reliably
- Button responds within 100ms
- Heartbeat prints even when button held

---

### Task 2.2: Priority Demonstration

```c
// Prove you understand priority:

Task HIGH (Priority 3):
- Runs every 1 second
- Busy-wait for 100ms (simulate work)
- Print "HIGH done"

Task LOW (Priority 1):
- Runs every 500ms
- Print "LOW running"

Expected behavior:
- HIGH preempts LOW
- LOW only runs when HIGH is blocked
```

**Deliverable:**

- Log output shows preemption clearly
- HIGH never waits for LOW
- LOW runs in between HIGH activations

---

### Task 2.3: Periodic vs Event-Driven

```c
// Create both types:

Periodic Task:
- vTaskDelayUntil() for exact 1 Hz
- Print with timestamp
- Must be precise (not drift)

Event Task:
- Wait for button press
- No polling, just block until event
- Print immediately when triggered
```

**Deliverable:**

- Periodic task prints at exact 1-second intervals (no drift)
- Event task responds instantly to button (not delayed)

---

### Task 2.4: Stack Overflow Detection

```c
// Deliberately cause stack overflow:

Task:
- Recursive function or huge local array
- Enable stack overflow checking
- Catch and log the error

Expected:
- vApplicationStackOverflowHook() called
- System doesn't crash silently
```

**Deliverable:**

- Overflow detected and logged
- You understand how to size stacks properly

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What does the scheduler do?**

   - (Decides which task runs based on priority and state)

2. **When does a context switch happen?**

   - (Higher priority task becomes ready, or running task blocks)

3. **What's the difference between vTaskDelay(100) and busy-wait 100ms?**

   - (vTaskDelay blocks and allows other tasks to run; busy-wait wastes CPU)

4. **Why does LOW priority task starve if HIGH never blocks?**

   - (Scheduler always picks highest ready task; LOW never gets CPU)

5. **What happens if stack is too small?**

   - (Stack overflow, memory corruption, crashes)

### Must Be Able to Draw:

```
Time →
Priority 3: HIGH  [RUN]──[BLOCK]────────[RUN]──[BLOCK]
Priority 2: MED   ─────[RUN]────[BLOCK]────[RUN]
Priority 1: LOW   ────────────[RUN]────────────[RUN]
                  ↑           ↑
                  Context     Context
                  Switch      Switch
```

---

## ❌ STILL LOCKED:

- Inter-task communication
- Shared resources
- Mutexes
- DMA

---

# 🟦 LEVEL 3: COMMUNICATION & SAFETY

**Theme:** "Tasks can cooperate without chaos"

## 📚 LEARN (Knowledge Requirements)

### Inter-Task Communication

- [ ] **Queue** (FIFO data passing)
- [ ] **Semaphore** (signaling, counting)
- [ ] **Mutex** (mutual exclusion)
- [ ] **Event groups** (multiple flags)

### Synchronization Problems

- [ ] **Race condition** (unsynchronized access)
- [ ] **Deadlock** (circular wait)
- [ ] **Priority inversion** (LOW blocks HIGH)
- [ ] **Starvation** (never gets resource)

### Mutex vs Semaphore

- [ ] **Mutex** = binary, has ownership
- [ ] **Semaphore** = counting, no ownership
- [ ] **Priority inheritance** (mutex only)

### ISR Rules

- [ ] **FromISR** functions required
- [ ] **No blocking** in ISR
- [ ] **Keep ISR short**
- [ ] **Defer work** to tasks

---

## ✅ DO (Tasks to Complete)

### Task 3.1: Queue Between Tasks

```c
// Producer-Consumer pattern:

Producer Task (Priority 2):
- Generate numbers 0-99
- Send to queue
- 100ms delay between sends

Consumer Task (Priority 1):
- Receive from queue
- Print received number
- Blocks waiting for data
```

**Deliverable:**

- All 100 numbers received in order
- Consumer blocks when queue empty
- No lost data

---

### Task 3.2: Demonstrate Race Condition

```c
// Break it first, then fix it:

Shared resource: uint32_t counter = 0;

Task A (Priority 1):
- Increment counter 1000 times
- NO protection

Task B (Priority 1):
- Increment counter 1000 times
- NO protection

Expected result WITHOUT mutex: counter < 2000 (race!)
```

**Deliverable:**

- Prove race condition (counter != 2000)
- Screenshot or log showing corruption
- Explain WHY it happened

---

### Task 3.3: Fix with Mutex

```c
// Now protect it:

Create mutex
Task A:
- Lock mutex
- Increment counter
- Unlock mutex

Task B:
- Lock mutex
- Increment counter
- Unlock mutex

Expected result: counter == 2000 (correct!)
```

**Deliverable:**

- Counter always equals 2000
- No race condition
- Tasks properly synchronized

---

### Task 3.4: ISR to Task Communication

```c
// Button ISR → Task via semaphore:

Button ISR:
- Give semaphore (xSemaphoreGiveFromISR)
- NO printing in ISR
- NO blocking in ISR

Handler Task:
- Take semaphore (blocks waiting)
- When released, print "Button!"
- Do slow work (100ms delay)
```

**Deliverable:**

- ISR completes in <10 μs
- Task does slow work safely
- No timing issues

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **When do you use a queue vs mutex?**

   - (Queue = pass data; Mutex = protect shared resource)

2. **What is a race condition?**

   - (Multiple tasks access shared data without synchronization → corruption)

3. **Why can't you use vTaskDelay() in an ISR?**

   - (ISRs can't block; would break the scheduler)

4. **What is priority inversion?**

   - (HIGH task blocked by LOW task that holds a mutex)

5. **How does a mutex prevent races?**

   - (Only one task can lock it at a time; others must wait)

### Must Be Able to Draw:

```
Task A          Queue (size=10)        Task B
[Producer] ──→ [xQueueSend] ──→ [xQueueReceive] → [Consumer]
              (blocks if full)     (blocks if empty)

Task C          Mutex              Task D
[Lock]    ←─ [xMutexTake] ──→  [Waits...]
[Use resource]      ↓
[Unlock]  ──→ [xMutexGive] ──→  [Now can lock]
```

---

## ❌ STILL LOCKED:

- DMA
- Performance optimization
- Audio pipeline

---

# 🟦 LEVEL 4: DMA & PERFORMANCE

**Theme:** "Move data without wasting CPU"

## 📚 LEARN (Knowledge Requirements)

### DMA Fundamentals

- [ ] **Direct Memory Access** (hardware copies data)
- [ ] **DMA channels** and streams
- [ ] **Circular mode** (ring buffer)
- [ ] **Normal mode** (one-shot)
- [ ] **Memory-to-memory** vs **peripheral-to-memory**

### DMA Configuration

- [ ] **Source** address
- [ ] **Destination** address
- [ ] **Data size** (byte, half-word, word)
- [ ] **Increment mode** (source/dest auto-increment)
- [ ] **Priority** (DMA vs CPU)

### DMA + Interrupts

- [ ] **Transfer complete** interrupt
- [ ] **Half transfer** interrupt (for double buffering)
- [ ] **Error** interrupt

### Double Buffering

- [ ] **Ping-pong buffer** concept
- [ ] **DMA fills buffer A while CPU reads buffer B**
- [ ] **Swap buffers** on half/full transfer

### Memory Management

- [ ] **Cache coherency** (not critical on M4, but concept)
- [ ] **Alignment** (word-aligned = faster)
- [ ] **DMA-safe memory** regions

---

## ✅ DO (Tasks to Complete)

### Task 4.1: UART RX with DMA

```c
// Receive data without CPU involvement:

Setup:
- UART RX DMA in circular mode
- Buffer size: 64 bytes
- DMA transfer complete interrupt

Task:
- Print received data when buffer fills
- NO polling
```

**Deliverable:**

- Send 64 bytes via serial terminal
- Data received automatically
- CPU was free during transfer
- Interrupt fires when buffer full

---

### Task 4.2: ADC with DMA

```c
// Continuous sampling:

Setup:
- ADC in continuous mode
- DMA circular buffer (256 samples)
- Half-transfer + transfer-complete interrupts

Task:
- Process first half while DMA fills second half
- Calculate average
- Print result
```

**Deliverable:**

- 256 samples collected automatically
- CPU only processes completed halves
- No samples lost

---

### Task 4.3: Memory-to-Memory DMA

```c
// Prove DMA is faster:

Test:
- Copy 1024 bytes
- Method 1: for loop (CPU)
- Method 2: DMA
- Measure time for both

Expected:
- DMA significantly faster
- CPU free during DMA transfer
```

**Deliverable:**

- Timing comparison logged:
  ```
  CPU copy: 850 μsDMA copy: 45 μs
  ```

---

### Task 4.4: Double Buffer Pattern

```c
// Classic ping-pong:

Setup:
- Two buffers (A and B), each 128 bytes
- DMA in circular mode
- Half + full transfer interrupts

Logic:
- DMA fills buffer A
- Half-transfer IRQ → switch to buffer B
- Process buffer A while DMA fills B
- Full-transfer IRQ → switch back to A
- Process buffer B while DMA fills A
```

**Deliverable:**

- Continuous data flow
- No overruns
- CPU processes one buffer while DMA fills other
- Log shows perfect alternation

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **Why use DMA instead of CPU for data transfer?**

   - (CPU freed for other work, faster, lower power)

2. **What is circular mode?**

   - (DMA automatically wraps around when buffer ends, continuous operation)

3. **What is double buffering?**

   - (Two buffers: process one while filling the other)

4. **When does half-transfer interrupt fire?**

   - (When DMA has filled 50% of circular buffer)

5. **What happens if you don't process buffer before DMA wraps?**

   - (Data overwritten, lost samples, buffer overrun)

### Must Be Able to Draw:

```
DMA Controller
     ↓
[Peripheral] → DMA → [Buffer A] → CPU processes
     ↓                     ↓
   ADC               [Buffer B] ← DMA fills
                           ↓
                    CPU processes ← DMA fills Buffer A

Half-Transfer IRQ: Switch to B, process A
Full-Transfer IRQ: Switch to A, process B
```

---

## ❌ STILL LOCKED:

- Real-time audio
- Continuous streams
- Overrun handling

---

# 🟦 LEVEL 5: AUDIO PIPELINE

**Theme:** "Handle continuous real-time data"

## 📚 LEARN (Knowledge Requirements)

### Digital Audio Basics

- [ ] **Sample rate** (e.g., 16 kHz, 44.1 kHz)
- [ ] **Bit depth** (8-bit, 16-bit, 24-bit)
- [ ] **PCM** (Pulse Code Modulation)
- [ ] **Mono vs stereo**

### Audio Interfaces

- [ ] **I2S** (Inter-IC Sound)
- [ ] **PDM** (Pulse Density Modulation, for MEMS mics)
- [ ] **Codec** (ADC/DAC for audio)

### Buffering Strategies

- [ ] **Ring buffer** (circular, continuous)
- [ ] **Buffer size vs latency** tradeoff
- [ ] **Overrun** (buffer fills faster than consumed)
- [ ] **Underrun** (buffer empties faster than filled)

### Real-Time Constraints

- [ ] **Deadline** (must process before next buffer fills)
- [ ] **Jitter** (timing variation)
- [ ] **Priority** (audio must be high)

### Audio File Formats

- [ ] **WAV** header structure
- [ ] **Raw PCM** (no header)

---

## ✅ DO (Tasks to Complete)

### Task 5.1: Microphone Capture (I2S + DMA)

```c
// Continuous audio input:

Hardware:
- I2S MEMS microphone (e.g., MP34DT01)
- Or analog mic + ADC

Setup:
- Sample rate: 16 kHz
- Bit depth: 16-bit
- DMA circular buffer: 512 samples
- Half + full transfer interrupts

Task:
- Capture audio continuously
- Toggle flag in ISR when buffer ready
- Process task reads and clears flag
```

**Deliverable:**

- Audio captured without gaps
- Buffer never overruns
- ISR executes in <50 μs
- Process task logs buffer count

---

### Task 5.2: WAV File Recording

```c
// Save to SD card or flash:

Requirements:
- Record 5 seconds of audio
- Save as valid WAV file
- WAV header with correct size
- 16 kHz, 16-bit, mono

Must work:
- Open file in Audacity/VLC
- Playback matches recording
```

**Deliverable:**

- WAV file plays correctly on PC
- File size matches: (sample_rate × duration × 2 bytes) + 44 header
- No corruption

---

### Task 5.3: Audio Playback

```c
// I2S or PWM output:

Setup:
- Read WAV file from storage
- Parse header
- DMA stream to I2S DAC or PWM

Task:
- Play audio smoothly
- No glitches
- No underruns
```

**Deliverable:**

- Audio plays clearly
- Volume control works
- Can start/stop

---

### Task 5.4: Record + UI Simultaneously

```c
// The real test:

Concurrent tasks:
- Audio task (Priority 3): Record continuously
- UI task (Priority 1): Button navigation, screen updates

Requirements:
- Press button to start/stop recording
- UI shows recording status
- Audio NEVER drops samples
- UI NEVER freezes
```

**Deliverable:**

- 30-second recording with user actively pressing buttons
- Zero audio overruns
- UI responsive (<100ms button response)
- Logs show both tasks running smoothly

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **Why is audio "real-time"?**

   - (Must process before next buffer fills, hard deadline)

2. **What happens during buffer overrun?**

   - (New data overwrites old data before it's processed → lost samples)

3. **How do you calculate buffer size for 50ms latency at 16 kHz?**

   - (16000 samples/sec × 0.05 sec = 800 samples)

4. **Why must audio task have higher priority than UI?**

   - (Audio has hard deadlines; UI delays are acceptable)

5. **What is a ring buffer?**

   - (Circular buffer where head wraps to start when reaching end)

### Must Be Able to Draw:

```
Mic → I2S → DMA → [Buffer 512 samples] → Process Task
                        ↓                       ↓
                  Half-Transfer            Save/Analyze
                  Full-Transfer            Save/Analyze

If Process Task is slow:
DMA wraps around → OVERRUN → lost data
```

---

## ❌ STILL LOCKED:

- Clean architecture
- Service separation
- OS structure

---

# 🟦 LEVEL 6: UI & RESPONSIVENESS

**Theme:** "Humans can interact under load"

## 📚 LEARN (Knowledge Requirements)

### UI Architecture

- [ ] **Event-driven design**
- [ ] **Input queue** (button/touch events)
- [ ] **Render vs update** separation
- [ ] **Frame rate** (30 Hz is fine for simple UI)

### LVGL (or similar)

- [ ] **Objects** (buttons, labels, screens)
- [ ] **Styles** and themes
- [ ] **Event callbacks**
- [ ] **Task integration** (lv_task_handler)

### Responsiveness

- [ ] **Perceived latency** (<100ms feels instant)
- [ ] **Frame drops** vs **timing violations**
- [ ] **Priority tuning** for UI task

### Input Handling

- [ ] **Debouncing** (already learned)
- [ ] **Long press** detection
- [ ] **Gesture** recognition (if touch)

---

## ✅ DO (Tasks to Complete)

### Task 6.1: Basic LVGL Integration

```c
// Get UI running:

Setup:
- LVGL initialized
- Display driver configured
- Touch/button input driver configured

Create:
- Home screen with 3 buttons
- Each button navigates to different screen
- Back navigation works
```

**Deliverable:**

- UI renders smoothly
- Navigation works
- No flickering

---

### Task 6.2: UI Task Separation

```c
// Proper architecture:

UI Task (Priority 1):
- lv_task_handler() every 20ms
- Process input events
- Update screen

Logic Task (Priority 2):
- Business logic
- Send UI updates via queue
- Never touches LVGL directly
```

**Deliverable:**

- UI and logic are separate tasks
- Communication via queue
- Clean separation of concerns

---

### Task 6.3: Responsive Under Load

```c
// The stress test:

Concurrent:
- Audio recording (Priority 3)
- Heavy computation task (Priority 2)
- UI updates (Priority 1)

Requirements:
- Button press → screen change <100ms
- Screen animations smooth
- No audio glitches
```

**Deliverable:**

- Record 30 seconds while navigating UI
- All inputs responsive
- Audio perfect
- UI smooth

---

### Task 6.4: Input Queue Pattern

```c
// Don't process input in ISR:

Button ISR:
- Put event in queue
- Return immediately

UI Task:
- Receive from queue
- Process event
- Update UI accordingly
```

**Deliverable:**

- Input handling is non-blocking
- Multiple rapid presses handled correctly
- No lost events

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **Why separate UI task from logic task?**

   - (Separation of concerns, easier debugging, better responsiveness)

2. **What is perceived latency?**

   - (Delay between user action and visible response)

3. **Why can't ISR update LVGL directly?**

   - (LVGL not re-entrant, might be in use by UI task → corruption)

4. **How often should lv_task_handler() run?**

   - (Every 10-20ms for smooth UI at ~50 Hz)

5. **What happens if UI task has higher priority than audio?**

   - (Audio overruns during heavy UI updates)

### Must Be Able to Draw:

```
Button ISR → [Input Queue] → UI Task → LVGL → Display
                                ↑
                          [Update Queue] ← Logic Task

Priority:
Audio (3) > Logic (2) > UI (1) > Idle (0)
```

---

## ❌ STILL LOCKED:

- OS architecture
- Clean layering
- Service APIs

---

# 🟦 LEVEL 7: OS STRUCTURE

**Theme:** "This is an operating system now"

## 📚 LEARN (Knowledge Requirements)

### Software Architecture

- [ ] **Layered design** (hardware → drivers → services → apps)
- [ ] **Abstraction** (hide implementation details)
- [ ] **API design** (clean interfaces)
- [ ] **Modularity** (components can be replaced)

### Separation of Concerns

- [ ] **Driver layer** = hardware abstraction
- [ ] **Service layer** = system functionality
- [ ] **App layer** = user-facing features
- [ ] **Never skip layers**

### Build System

- [ ] **Folder structure** organization
- [ ] **Header file** design (.h declarations)
- [ ] **Source file** (.c implementations)
- [ ] **Makefile** or project organization

---

## ✅ DO (Tasks to Complete)

### Task 7.1: Reorganize Code Structure

```
Project structure:

/drivers
    /gpio
        gpio.h
        gpio.c
    /i2s
        i2s.h
        i2s.c
    /uart
        uart.h
        uart.c

/services
    /audio
        audio_service.h
        audio_service.c
    /storage
        storage_service.h
        storage_service.c
    /ui
        ui_service.h
        ui_service.c

/apps
    /recorder
        recorder.h
        recorder.c
    /playback
        playback.h
        playback.c

/os
    os_init.c
    main.c

Rules:
- Apps NEVER include driver headers
- Apps ONLY call service APIs
- Services call drivers
```

**Deliverable:**

- Code compiles with new structure
- No direct hardware access in apps
- Clean boundaries

---

### Task 7.2: Service API Design

```c
// Audio service example:

// audio_service.h
typedef enum {
    AUDIO_OK,
    AUDIO_ERROR,
    AUDIO_BUSY
} audio_status_t;

audio_status_t audio_init(void);
audio_status_t audio_start_recording(void);
audio_status_t audio_stop_recording(void);
audio_status_t audio_play(const char* filename);

// Apps call these APIs:
// audio_start_recording();
// NOT: I2S_Start_DMA(...);
```

**Deliverable:**

- Clean, documented APIs
- Apps use services, not drivers
- Services hide all hardware details

---

### Task 7.3: Boot Sequence

```c
// Defined startup order:

main() {
    // 1. Hardware init
    SystemClock_Config();

    // 2. Driver init
    gpio_init();
    uart_init();
    i2s_init();

    // 3. Service init
    audio_service_init();
    storage_service_init();
    ui_service_init();

    // 4. App init
    recorder_app_init();

    // 5. Start scheduler
    vTaskStartScheduler();
}
```

**Deliverable:**

- Explicit initialization order
- Each layer initializes correctly
- System boots reliably

---

### Task 7.4: Documentation

```c
// Every service has README:

/services/audio/README.md
---
# Audio Service

## Purpose
Handles audio recording and playback

## Dependencies
- I2S driver
- DMA driver
- Storage service (for saving files)

## API
...

## Usage Example
...

## Known Limitations
- Max 60 seconds recording
- 16 kHz only
```

**Deliverable:**

- Each major component documented
- Dependencies clear
- Usage examples provided

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **Why can't apps call drivers directly?**

   - (Breaks abstraction, makes hardware changes affect all apps)

2. **What is a service?**

   - (System functionality that apps can use, hides implementation)

3. **What goes in a driver vs service?**

   - (Driver = hardware control; Service = high-level functionality)

4. **Why does init order matter?**

   - (Services depend on drivers, apps depend on services)

5. **How is this different from a monolithic firmware?**

   - (Clear layers, clean interfaces, modular, maintainable)

### Must Be Able to Draw:

```
┌─────────────────────────────┐
│         Apps                │  recorder, playback, menu
├─────────────────────────────┤
│       Services              │  audio, storage, ui
├─────────────────────────────┤
│        Drivers              │  i2s, dma, gpio, uart
├─────────────────────────────┤
│    FreeRTOS Kernel          │  tasks, queues, scheduler
├─────────────────────────────┤
│       Hardware              │  STM32F429
└─────────────────────────────┘

Communication flows DOWN only
(apps → services → drivers → hardware)
```

---

## ❌ STILL LOCKED:

- Understanding limits
- Phase 2 (Linux)

---

# 🟦 LEVEL 8: LIMITS & TRUTH

**Theme:** "I know when to stop"

## 📚 LEARN (Knowledge Requirements)

### System Limits

- [ ] **RAM limit** (256 KB on F429)
- [ ] **Flash limit** (~2 MB)
- [ ] **CPU speed** (180 MHz Cortex-M4)
- [ ] **No MMU** (memory protection limited)
- [ ] **No NPU** (no AI acceleration)

### Why Offline Dictation Fails Here

- [ ] **Model size** (even tiny STT = 50+ MB)
- [ ] **RAM requirements** (working set > 100 MB)
- [ ] **Compute requirements** (real-time inference impossible)
- [ ] **Accuracy issues** (even if it ran, quality terrible)

### What Linux Solves

- [ ] **Virtual memory** (MMU)
- [ ] **Process isolation**
- [ ] **Larger RAM** (1-2 GB)
- [ ] **File system** maturity
- [ ] **Library ecosystem**

---

## ✅ DO (Tasks to Complete)

### Task 8.1: Force Buffer Overrun

```c
// Deliberately break it:

Audio recording at 16 kHz
UI task doing heavy computation
Logging over UART aggressively

Expected:
- Audio buffer overrun
- DMA wraps before processing complete
- Lost samples
```

**Deliverable:**

- Detect overrun condition
- Log warning: "OVERRUN: Lost 128 samples"
- Explain exactly why it happened

---

### Task 8.2: Measure Resource Usage

```c
// Quantify what you have:

Measure:
- Task stack usage (uxTaskGetStackHighWaterMark)
- Heap usage (xPortGetFreeHeapSize)
- CPU utilization (idle task time)

Report:
Audio task: 512 bytes stack (80% used)
UI task: 1024 bytes stack (60% used)
Heap free: 45 KB / 64 KB
CPU utilization: 45%
```

**Deliverable:**

- Full resource report
- Know exactly what's available
- Understand limits

---

### Task 8.3: Estimate STT Requirements

```c
// Research what offline dictation needs:

Find:
- Whisper tiny model size (~75 MB)
- RAM required for inference (~200 MB)
- CPU requirements (billion ops per second)

Compare to STM32F429:
- Flash: 2 MB (model doesn't fit)
- RAM: 256 KB (working set doesn't fit)
- CPU: 180 MHz (1000x too slow)

Conclusion: NOT FEASIBLE
```

**Deliverable:**

- Written analysis (1 page)
- Math showing why it fails
- Quote sources

---

### Task 8.4: Phase 1 Reflection Document

```markdown
# Black Hand OS - Phase 1 Complete

## What I Built

- [ ] Audio recording & playback
- [ ] UI with menu navigation
- [ ] Multi-task RTOS architecture
- [ ] Service-oriented design

## What I Learned

- How real-time systems work
- DMA and performance optimization
- Why abstraction layers matter
- System resource constraints

## Limits Encountered

- RAM too small for ML models
- No MMU for process isolation
- Flash too small for large assets

## Why Linux is Next

- Need 1+ GB RAM for offline STT
- Need MMU for complex software
- Need mature file system
- Need library ecosystem

## Conclusion

Phase 1 is a complete, working OS for constrained hardware.
Linux is required for offline dictation.
I understand this technically, not emotionally.
```

**Deliverable:**

- Honest reflection
- Technical reasoning
- No guilt about moving on

---

## 🔓 UNLOCK CONDITIONS (PHASE 1 COMPLETE)

### Final Quiz:

1. **Why can't STM32F429 do offline dictation?**

   - (Model size + RAM + CPU all insufficient by orders of magnitude)

2. **What does Phase 1 teach that Linux can't?**

   - (How things work with no safety net, real constraints, low-level control)

3. **Is Phase 1 "real" or just practice?**

   - (It's real — a complete OS for its hardware class)

4. **When should you use RTOS vs Linux?**

   - (RTOS for hard real-time, simple, low power; Linux for complex software, large RAM, ecosystem)

5. **What will you keep from Phase 1 when moving to Linux?**

   - (Architecture thinking, service design, understanding of constraints)

### Must Be Able to Say Out Loud:

```
"I built Black Hand OS on STM32F429 with FreeRTOS.
It handles audio recording, playback, and a responsive UI.
I understand exactly why offline dictation requires Linux:
the model won't fit, the RAM is insufficient, and the CPU is too slow.
Phase 1 is complete. Phase 2 is justified."
```

---

## 🏁 CONGRATULATIONS - PHASE 1 COMPLETE

You are now qualified to:

- Design embedded RTOS systems
- Optimize real-time performance
- Architect service-oriented firmware
- **Move to Phase 2: Linux**

---

# 📊 PHASE 1 SUMMARY CHECKLIST

Print this out. Check each box when complete:

```
LEVEL 1: BOOT & CONTROL
□ Reliable boot sequence
□ Timer interrupt LED
□ Debounced button input
□ UART debug logging

LEVEL 2: RTOS CORE
□ Three concurrent tasks
□ Priority demonstration
□ Periodic vs event-driven
□ Stack overflow detection

LEVEL 3: COMMUNICATION
□ Queue producer-consumer
□ Race condition demonstrated
□ Fixed with mutex
□ ISR to task via semaphore

LEVEL 4: DMA & PERFORMANCE
□ UART RX with DMA
□ ADC continuous sampling
□ Memory-to-memory DMA benchmark
□ Double buffer pattern

LEVEL 5: AUDIO PIPELINE
□ Microphone capture (I2S + DMA)
□ WAV file recording
□ Audio playback
□ Record + UI concurrently

LEVEL 6: UI & RESPONSIVENESS
□ LVGL integration
□ UI task separation
□ Responsive under load
□ Input queue pattern

LEVEL 7: OS STRUCTURE
□ Reorganized folder structure
□ Service API design
□ Boot sequence defined
□ Component documentation

LEVEL 8: LIMITS & TRUTH
□ Force buffer overrun
□ Measure resource usage
□ STT requirements analysis
□ Phase 1 reflection written
```

**When all boxes checked → PHASE 1 DONE → Move to Phase 2**

---

# 🎮 BLACK HAND OS - PHASE 2 COMPLETE ROADMAP

## PHASE 2 OVERVIEW

**Platform:** Embedded Linux (Cortex-A SoC, 1GB+ RAM)  
**Codename:** Black Hand OS — Linux Edition  
**Win Condition:** Offline voice-to-text working on custom Linux phone OS

## Prerequisites:

- ✅ Phase 1 complete (all 8 levels)
- ✅ You understand RTOS, drivers, services, architecture
- ✅ You know WHY Linux is needed (not just "Linux is better")

---

# 🟦 LEVEL 1: LINUX FOUNDATIONS

**Theme:** "Linux is just a kernel"

## 📚 LEARN (Knowledge Requirements)

### Linux Architecture

- [ ] **Kernel** vs **userspace**
- [ ] **System calls** (kernel API)
- [ ] **Processes** vs **threads**
- [ ] **Virtual memory** (MMU, paging)
- [ ] **File system** hierarchy (/bin, /etc, /dev, etc.)

### Boot Process

- [ ] **Bootloader** (U-Boot)
- [ ] **Kernel** loading
- [ ] **Device tree** (.dtb)
- [ ] **Init process** (PID 1)
- [ ] **Userspace** startup

### Development Environment

- [ ] **Cross-compilation** (host vs target)
- [ ] **Toolchain** (gcc, binutils, glibc)
- [ ] **Sysroot** concept
- [ ] **SSH** for remote development

### Build Systems

- [ ] **Buildroot** (simple, minimal)
- [ ] **Yocto** (powerful, complex)
- [ ] **Config files** and customization

---

## ✅ DO (Tasks to Complete)

### Task 1.1: Choose Development Board

```
Options (pick ONE to start):

Option A: Raspberry Pi 4 (2GB+)
- Easy, well-supported
- Fast iteration
- Good for prototyping

Option B: BeagleBone Black
- More embedded-like
- Good learning platform

Option C: Custom SoC dev board
- i.MX6/7, RK3399, etc.
- Closer to final hardware

Decision criteria:
- Available now
- Good Linux support
- 1GB+ RAM minimum
```

**Deliverable:**

- Board acquired
- Basic boot achieved (stock Linux image)
- SSH access working
- Can log in and run commands

---

### Task 1.2: Build Minimal Linux with Buildroot

```bash
# Steps:

1. Download Buildroot
2. Configure for your board:
   - make <board>_defconfig
3. Customize:
   - Enable packages you need
   - Set root password
   - Enable SSH
4. Build:
   - make
5. Flash to SD card
6. Boot

Required packages:
- Dropbear (SSH)
- Busybox (shell utilities)
- ALSA utilities
```

**Deliverable:**

- Custom Linux boots on board
- < 100 MB root filesystem
- SSH works
- Can run basic commands
- You built it yourself (not pre-made image)

---

### Task 1.3: "Hello World" Service

```c
// Create a simple daemon:

// hello_service.c
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>

int main(void) {
    openlog("hello_service", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Hello service started");

    while (1) {
        syslog(LOG_INFO, "Still alive");
        sleep(5);
    }

    return 0;
}

Compile for target:
$ arm-linux-gcc hello_service.c -o hello_service

Deploy:
- Copy to /usr/bin/
- Create init script
- Auto-start on boot
```

**Deliverable:**

- Service runs on boot
- Logs to syslog
- Can view with `logread` or `dmesg`
- Can stop/start manually

---

### Task 1.4: Understand Kernel vs Userspace

```
Demonstrate understanding:

1. Write a kernel module (simple):
   - Prints "Hello from kernel" on load
   - insmod/rmmod works

2. Write userspace program:
   - Prints "Hello from userspace"
   - Calls kernel via system call

3. Explain difference:
   - Where does each run?
   - What can each do?
   - Why the separation?
```

**Deliverable:**

- Kernel module loads/unloads
- Userspace program runs
- Written explanation (1 page):
  - What is a system call?
  - Why can't userspace touch hardware directly?
  - What does the kernel provide?

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What is the kernel's job?**

   - (Manage hardware, schedule processes, handle I/O, provide abstractions)

2. **What is userspace?**

   - (Where applications run, protected from direct hardware access)

3. **What happens during boot?**

   - (Bootloader → load kernel → mount rootfs → start init → start services)

4. **Why cross-compile?**

   - (Target CPU ≠ development CPU, must build for ARM on x86)

5. **What is a device tree?**

   - (Hardware description file, tells kernel what devices exist)

### Must Be Able to Draw:

```
┌──────────────────────────┐
│   Applications           │  Your phone apps
├──────────────────────────┤
│   Libraries (libc)       │  System libraries
├──────────────────────────┤  ← System call interface
│   Linux Kernel           │  Process, memory, drivers
├──────────────────────────┤
│   Hardware               │  CPU, RAM, peripherals
└──────────────────────────┘
```

---

## ❌ STILL LOCKED:

- Custom init system
- Audio on Linux
- Service architecture
- Phone OS features

---

# 🟦 LEVEL 2: CUSTOM INIT & SERVICES

**Theme:** "Your OS starts here"

## 📚 LEARN (Knowledge Requirements)

### Init Systems

- [ ] **Init** (PID 1, first userspace process)
- [ ] **Traditional init** (simple scripts)
- [ ] **systemd** (complex, feature-rich)
- [ ] **BusyBox init** (minimal, embedded-friendly)
- [ ] **Custom init** (roll your own)

### Service Management

- [ ] **Daemon** vs **process**
- [ ] **Background services**
- [ ] **Dependencies** (service A needs service B)
- [ ] **Logging** (syslog)

### IPC Mechanisms

- [ ] **Unix domain sockets**
- [ ] **Named pipes (FIFOs)**
- [ ] **Shared memory**
- [ ] **Message queues**
- [ ] **D-Bus** (desktop standard)

### Process Management

- [ ] **fork()** and **exec()**
- [ ] **Signals** (SIGTERM, SIGKILL)
- [ ] **Process groups**
- [ ] **Daemon creation** (double fork, setsid)

---

## ✅ DO (Tasks to Complete)

### Task 2.1: Custom Init Script

```bash
#!/bin/sh
# /sbin/init (your custom init)

echo "Black Hand OS booting..."

# Mount essential filesystems
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev

# Start essential services
/usr/sbin/syslogd
/usr/sbin/dropbear  # SSH

# Start your phone services
/usr/bin/audio_service &
/usr/bin/ui_service &

# Keep init running
while true; do
    sleep 1
done
```

**Deliverable:**

- Custom init boots system
- Services start in correct order
- Logs show startup sequence
- System stable

---

### Task 2.2: Service Template

```c
// service_template.c
// Pattern for all your services

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

static volatile int keep_running = 1;

void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        syslog(LOG_INFO, "Shutting down...");
        keep_running = 0;
    }
}

int main(void) {
    // Daemonize
    if (fork() != 0) exit(0);  // Parent exits
    setsid();                   // New session
    if (fork() != 0) exit(0);  // Parent exits again

    // Setup
    openlog("my_service", LOG_PID, LOG_DAEMON);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    syslog(LOG_INFO, "Service started");

    // Main loop
    while (keep_running) {
        // Do work
        sleep(1);
    }

    syslog(LOG_INFO, "Service stopped");
    closelog();
    return 0;
}
```

**Deliverable:**

- Template service runs as daemon
- Responds to SIGTERM gracefully
- Logs properly
- Use this for all future services

---

### Task 2.3: IPC Between Services

```c
// Implement Unix socket IPC:

Server (audio_service):
- Create socket: /tmp/audio.sock
- Listen for commands:
  - "START_RECORD"
  - "STOP_RECORD"
  - "PLAY file.wav"
- Send responses

Client (ui_service):
- Connect to /tmp/audio.sock
- Send commands
- Receive responses
- Never block UI
```

**Deliverable:**

- Two services communicate
- UI can control audio service
- Clean protocol defined
- Error handling works

---

### Task 2.4: Service Manager

```c
// service_manager.c
// Monitors and restarts crashed services

#include <sys/wait.h>

typedef struct {
    const char *name;
    const char *path;
    pid_t pid;
} service_t;

service_t services[] = {
    {"audio", "/usr/bin/audio_service", 0},
    {"ui", "/usr/bin/ui_service", 0},
};

void start_service(service_t *svc) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(svc->path, svc->name, NULL);
        exit(1);
    }
    svc->pid = pid;
    syslog(LOG_INFO, "Started %s (PID %d)", svc->name, pid);
}

int main(void) {
    // Start all services
    for (int i = 0; i < NUM_SERVICES; i++) {
        start_service(&services[i]);
    }

    // Monitor and restart on crash
    while (1) {
        int status;
        pid_t pid = wait(&status);

        // Find which service crashed
        for (int i = 0; i < NUM_SERVICES; i++) {
            if (services[i].pid == pid) {
                syslog(LOG_WARNING, "%s crashed, restarting",
                       services[i].name);
                start_service(&services[i]);
            }
        }
    }
}
```

**Deliverable:**

- Service manager starts services
- Detects crashes
- Auto-restarts
- Logs everything

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What is PID 1?**

   - (First process, init, parent of all processes)

2. **What is a daemon?**

   - (Background service, detached from terminal, runs continuously)

3. **How do services communicate?**

   - (IPC: sockets, pipes, shared memory, message queues)

4. **Why use Unix sockets vs network sockets?**

   - (Faster, local only, no network overhead, better security)

5. **What happens if init crashes?**

   - (Kernel panic, system unusable)

### Must Be Able to Draw:

```
Init (PID 1)
    ├→ audio_service (PID 45)
    │      ↕ Unix socket
    ├→ ui_service (PID 46)
    │      ↕
    ├→ storage_service (PID 47)
    └→ service_manager (PID 48)
           └→ monitors all, restarts on crash
```

---

## ❌ STILL LOCKED:

- Audio on Linux
- Voice-to-text
- UI framework

---

# 🟦 LEVEL 3: AUDIO PIPELINE ON LINUX

**Theme:** "ALSA is your friend"

## 📚 LEARN (Knowledge Requirements)

### ALSA (Advanced Linux Sound Architecture)

- [ ] **Sound cards** and **devices**
- [ ] **PCM** (Pulse Code Modulation) interface
- [ ] **Playback** vs **capture**
- [ ] **Periods** and **buffer size**
- [ ] **alsa-lib** API

### Audio Configuration

- [ ] **/proc/asound/** structure
- [ ] **arecord** / **aplay** tools
- [ ] **alsactl** for settings
- [ ] **asound.conf** configuration

### Audio Formats

- [ ] **Sample rate** (8k, 16k, 44.1k, 48k)
- [ ] **Channels** (mono, stereo)
- [ ] **Format** (S16_LE, etc.)
- [ ] **Interleaved** vs **non-interleaved**

### Buffering

- [ ] **Period size** (latency unit)
- [ ] **Buffer size** (total)
- [ ] **Underrun** / **overrun** handling

---

## ✅ DO (Tasks to Complete)

### Task 3.1: Get Audio Hardware Working

```bash
# Verify audio devices exist:
$ cat /proc/asound/cards
$ arecord -l  # List capture devices
$ aplay -l    # List playback devices

# Test recording:
$ arecord -D hw:0,0 -f S16_LE -r 16000 -c 1 test.wav

# Test playback:
$ aplay test.wav

# If audio doesn't work, debug:
- Device tree configuration
- Kernel modules loaded
- ALSA configuration
```

**Deliverable:**

- Audio recording works
- Audio playback works
- Know device names (hw:0,0, etc.)
- Clean audio (no distortion)

---

### Task 3.2: ALSA C API Recording

```c
// alsa_record.c
#include <alsa/asoundlib.h>

#define SAMPLE_RATE 16000
#define CHANNELS 1
#define FORMAT SND_PCM_FORMAT_S16_LE

int main(void) {
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    int16_t buffer[1024];

    // Open device
    snd_pcm_open(&handle, "default",
                 SND_PCM_STREAM_CAPTURE, 0);

    // Configure
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params,
        SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, FORMAT);
    snd_pcm_hw_params_set_channels(handle, params, CHANNELS);
    snd_pcm_hw_params_set_rate_near(handle, params,
        &SAMPLE_RATE, 0);
    snd_pcm_hw_params(handle, params);

    // Record loop
    while (1) {
        int frames = snd_pcm_readi(handle, buffer, 1024);
        if (frames < 0) {
            snd_pcm_recover(handle, frames, 0);
        } else {
            // Process audio
            process_audio(buffer, frames);
        }
    }

    snd_pcm_close(handle);
    return 0;
}
```

**Deliverable:**

- ALSA recording works
- Handles overruns gracefully
- Continuous capture stable
- Compiles and runs

---

### Task 3.3: Audio Service (Linux Version)

```c
// audio_service.c
// Linux version with ALSA

Features:
- Start/stop recording via socket commands
- Save to WAV file
- Playback WAV files
- Handle multiple requests (queue)

Architecture:
- Main thread: IPC server
- Record thread: ALSA capture
- Playback thread: ALSA playback
```

**Deliverable:**

- Service runs as daemon
- Accepts commands via socket
- Recording works
- Playback works
- Concurrent operations (record + UI commands)

---

### Task 3.4: Real-time Audio Processing

```c
// Prove low-latency capability:

Setup:
- ALSA with small period size (256 samples)
- Process each period immediately
- Calculate latency

Test:
- Record audio
- Apply simple processing (volume adjust)
- Play back immediately
- Measure input → output delay

Target: < 50ms total latency
```

**Deliverable:**

- Low-latency audio loop works
- Latency measured and logged
- No dropouts during processing
- Understand period/buffer tradeoffs

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What is ALSA?**

   - (Linux kernel audio framework, drivers + userspace lib)

2. **What is a period?**

   - (Chunk of audio buffer, interrupt fires per period)

3. **How do you calculate latency?**

   - (Period size / sample rate, e.g., 256 / 16000 = 16ms)

4. **What causes underrun?**

   - (Consumer doesn't read fast enough, buffer empties)

5. **Why use threads for audio vs processes?**

   - (Shared memory, lower overhead, easier synchronization)

### Must Be Able to Draw:

```
Mic → ALSA Driver → Kernel Ring Buffer
                         ↓
                    snd_pcm_readi()
                         ↓
                  Audio Service (userspace)
                         ↓
                  Process / Save / Send
```

---

## ❌ STILL LOCKED:

- Speech recognition
- ML integration
- UI framework

---

# 🟦 LEVEL 4: OFFLINE SPEECH-TO-TEXT

**Theme:** "AI on the edge"

## 📚 LEARN (Knowledge Requirements)

### Speech Recognition Basics

- [ ] **Acoustic model** (audio → phonemes)
- [ ] **Language model** (phonemes → words)
- [ ] **Decoder** (search for best sequence)
- [ ] **Beam search** algorithm

### Offline STT Engines

- [ ] **Whisper** (OpenAI, heavy but accurate)
- [ ] **Vosk** (lightweight, offline)
- [ ] **DeepSpeech** (Mozilla, discontinued but works)
- [ ] **Pocketsphinx** (old but tiny)

### Model Considerations

- [ ] **Model size** vs **accuracy**
- [ ] **Inference time** vs **latency**
- [ ] **Quantization** (reduce size/speed)
- [ ] **Language support**

### Integration

- [ ] **C/C++ API** for embedding
- [ ] **Audio preprocessing** (resampling, normalization)
- [ ] **Streaming** vs **batch** recognition

---

## ✅ DO (Tasks to Complete)

### Task 4.1: Choose STT Engine

```
Research and decide:

Option A: Vosk (RECOMMENDED)
+ Lightweight (~50 MB model)
+ Good accuracy
+ Offline
+ C++ API
+ Active development
- Need to compile for ARM

Option B: Whisper
+ Best accuracy
+ Many languages
- Heavy (>100 MB)
- Slower inference
- Needs more RAM

Option C: Pocketsphinx
+ Tiny (~10 MB)
+ Very fast
- Poor accuracy
- Old

Decision criteria:
- Fits in RAM
- Runs in real-time
- Acceptable accuracy
```

**Deliverable:**

- Engine chosen and justified
- Compiled for target
- Simple test program runs
- "Hello world" recognized

---

### Task 4.2: Integrate Vosk (Example)

```cpp
// stt_service.cpp
#include "vosk_api.h"

class STTService {
private:
    VoskModel *model;
    VoskRecognizer *recognizer;

public:
    STTService(const char *model_path) {
        model = vosk_model_new(model_path);
        recognizer = vosk_recognizer_new(model, 16000.0);
    }

    std::string process_audio(int16_t *data, size_t len) {
        if (vosk_recognizer_accept_waveform(recognizer,
            (const char*)data, len * 2)) {
            const char *result =
                vosk_recognizer_result(recognizer);
            return parse_json(result);
        }
        return "";
    }

    std::string finish() {
        const char *result =
            vosk_recognizer_final_result(recognizer);
        return parse_json(result);
    }
};
```

**Deliverable:**

- Vosk integrated
- Recognizes speech from mic
- Returns text
- Handles partial results

---

### Task 4.3: STT Service with IPC

```cpp
// Full service:

Features:
- Listen on Unix socket
- Accept audio data or "start/stop" commands
- Return transcribed text
- Handle concurrent requests

Commands:
- START_STREAM
- AUDIO_DATA [bytes]
- STOP_STREAM → returns text
- TRANSCRIBE_FILE file.wav → returns text

Protocol:
Request: COMMAND [data]
Response: OK|ERROR [result]
```

**Deliverable:**

- Service runs as daemon
- UI can send audio, get text back
- Streaming recognition works
- File transcription works

---

### Task 4.4: Optimize for Performance

```cpp
// Measure and optimize:

Benchmarks:
- Time to load model
- Time to process 1 second of audio
- Memory usage during inference
- CPU utilization

Optimizations:
- Quantized model (if available)
- Compile with -O3
- Use NEON (ARM SIMD) if supported
- Reduce logging overhead

Target:
- < 2 seconds to transcribe 5-second clip
- < 500 MB RAM total
```

**Deliverable:**

- Performance metrics logged
- Optimizations applied
- Real-time factor < 0.5 (processes faster than real-time)
- Memory stays within budget

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What is the difference between streaming and batch STT?**

   - (Streaming = real-time, partial results; Batch = full audio, final result)

2. **Why does model size matter?**

   - (RAM required, load time, storage space)

3. **What is quantization?**

   - (Reducing precision, e.g., float32 → int8, smaller/faster)

4. **How do you measure real-time factor?**

   - (Processing time / audio duration, <1.0 = faster than real-time)

5. **Why offline STT needs Linux, not MCU?**

   - (Large models, complex computation, lots of RAM)

### Must Be Able to Draw:

```
Audio (PCM 16kHz)
    ↓
Preprocessing (normalize, resample)
    ↓
Feature Extraction (MFCC, mel spectrogram)
    ↓
Acoustic Model (neural network)
    ↓
Language Model (decode to words)
    ↓
Text Output
```

---

## ❌ STILL LOCKED:

- UI framework
- Full phone integration
- End-to-end demo

---

# 🟦 LEVEL 5: UI FRAMEWORK

**Theme:** "The user sees this"

## 📚 LEARN (Knowledge Requirements)

### UI Options on Linux

- [ ] **Qt** (C++, powerful, heavy)
- [ ] **GTK** (C, mature, desktop-oriented)
- [ ] **LVGL on Linux** (lightweight, embedded-friendly)
- [ ] **SDL** (low-level, game-like)
- [ ] **Custom framebuffer** (direct rendering)

### Display Systems

- [ ] **Framebuffer** (/dev/fb0)
- [ ] **DRM/KMS** (modern)
- [ ] **X11** (legacy, heavy)
- [ ] **Wayland** (modern compositor)

### Input Handling

- [ ] **Touchscreen** (/dev/input/eventX)
- [ ] **Buttons** (GPIO or input events)
- [ ] **Event loop** integration

### UI Architecture

- [ ] **MVC** (Model-View-Controller)
- [ ] **Event-driven** design
- [ ] **State machines** for screens

---

## ✅ DO (Tasks to Complete)

### Task 5.1: Choose UI Framework

```
Decision matrix:

LVGL on Linux:
+ Already know LVGL from Phase 1
+ Lightweight
+ Works on framebuffer
+ Easy integration
- Less polished than Qt

Qt for Embedded:
+ Professional
+ Rich widgets
+ Good tooling
- Heavy (~50+ MB)
- Longer learning curve

Recommendation: LVGL
(Faster to market, familiar, sufficient for phone UI)
```

**Deliverable:**

- Framework chosen
- Compiles for target
- Simple "Hello World" renders on screen
- Touch/button input works

---

### Task 5.2: LVGL on Linux Framebuffer

```c
// main_ui.c
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"

int main(void) {
    lv_init();

    // Display setup
    fbdev_init();
    static lv_disp_draw_buf_t disp_buf;
    static lv_color_t buf[DISP_BUF_SIZE];
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    lv_disp_drv_register(&disp_drv);

    // Input setup
    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);

    // Create UI
    create_home_screen();

    // Main loop
    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
}
```

**Deliverable:**

- LVGL runs on Linux
- Renders to framebuffer
- Touch input works
- Smooth at 30+ FPS

---

### Task 5.3: Phone UI Shell

```c
// Screens:

1. Home Screen
   - Time/date
   - Battery indicator
   - Signal strength (placeholder)
   - Menu button

2. Main Menu
   - Call
   - Messages
   - Voice Recorder
   - Settings

3. Voice Recorder
   - Record button
   - Stop button
   - Transcription display
   - Save button

4. Settings
   - Volume
   - Display
   - About
```

**Deliverable:**

- All screens implemented
- Navigation works
- Consistent visual style
- Responsive (<100ms)

---

### Task 5.4: UI ↔ Service Communication

```c
// UI Service Pattern:

UI Process:
- Renders screens
- Handles input
- Sends commands to backend services
- Receives updates via IPC

Backend Services:
- audio_service
- stt_service
- storage_service

IPC:
- Unix sockets
- Async callbacks
- Non-blocking UI
```

**Deliverable:**

- UI never blocks
- Can send commands while recording
- Updates appear immediately
- Clean separation of UI and logic

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What is a framebuffer?**

   - (Direct memory-mapped display buffer, /dev/fb0)

2. **Why use event-driven UI?**

   - (Responsive, efficient, doesn't waste CPU polling)

3. **How do you prevent UI blocking?**

   - (Never do slow work in UI thread, use async IPC)

4. **What is MVC?**

   - (Model-View-Controller, separates data/logic/presentation)

5. **Why LVGL over Qt for embedded?**

   - (Lighter, simpler, better for resource-constrained)

### Must Be Able to Draw:

```
User Touch → Input Driver → LVGL Event
                                ↓
                          Event Handler
                                ↓
                    Send command via socket
                                ↓
                          Backend Service
                                ↓
                        Process & Respond
                                ↓
                    Update UI (callback)
```

---

## ❌ STILL LOCKED:

- End-to-end integration
- Phone features
- System optimization

---

# 🟦 LEVEL 6: PHONE FEATURES

**Theme:** "It's actually a phone now"

## 📚 LEARN (Knowledge Requirements)

### Voice Recorder App

- [ ] **Push-to-talk** interaction
- [ ] **WAV file** management
- [ ] **Metadata** (timestamp, duration)
- [ ] **List/playback** saved recordings

### Transcription Flow

- [ ] **Record** → **transcribe** → **display**
- [ ] **Edit** transcription
- [ ] **Save** as text
- [ ] **Share** (future: SMS)

### Storage Management

- [ ] **File system** structure
- [ ] **Recordings** directory
- [ ] **Metadata** database (SQLite)
- [ ] **Cleanup** old files

### Battery & Power

- [ ] **CPU governor** (performance vs power)
- [ ] **Display brightness**
- [ ] **Sleep mode**
- [ ] **Low power states**

---

## ✅ DO (Tasks to Complete)

### Task 6.1: Voice Recorder App

```
Features:

Recording:
- Press and hold to record
- Release to stop
- Show timer during recording
- Save to /recordings/YYYY-MM-DD_HH-MM-SS.wav

Playback:
- List all recordings
- Tap to play
- Delete option
- Show duration

Transcription:
- "Transcribe" button per recording
- Show loading indicator
- Display text when done
- Edit and save
```

**Deliverable:**

- Full recorder app works
- Can record, play, delete
- Transcription integrated
- Files saved properly

---

### Task 6.2: Notes App with Voice Input

```
Features:

Create Note:
- Text input (keyboard, future)
- Voice input (record → transcribe → insert)
- Title + body
- Timestamp

View Notes:
- List view
- Tap to open/edit
- Search (future)

Voice Input Flow:
- Tap mic button
- Record (max 30 seconds)
- Auto-transcribe
- Insert text at cursor
- Can re-record if wrong
```

**Deliverable:**

- Notes app functional
- Voice → text works seamlessly
- UX feels natural
- Data persists (SQLite)

---

### Task 6.3: Storage Service

```c
// storage_service.c

Features:
- Manage recordings directory
- SQLite database:
  - Recordings table
  - Notes table
  - Metadata
- CRUD operations via IPC

Database schema:
CREATE TABLE recordings (
    id INTEGER PRIMARY KEY,
    filename TEXT,
    timestamp INTEGER,
    duration REAL,
    transcription TEXT
);

CREATE TABLE notes (
    id INTEGER PRIMARY KEY,
    title TEXT,
    body TEXT,
    created INTEGER,
    modified INTEGER
);
```

**Deliverable:**

- Storage service runs
- Database operations work
- UI can save/load data
- Handles errors gracefully

---

### Task 6.4: Settings & System

```
Settings App:

Audio:
- Microphone gain
- Speaker volume
- Sample rate (16k/44.1k)

Display:
- Brightness
- Theme (light/dark)
- Screen timeout

System:
- About (OS version, storage, RAM)
- Factory reset
- Update (future)

Power Management:
- Screen off after 30s idle
- Suspend if no activity
- Wake on button press
```

**Deliverable:**

- Settings app implemented
- Changes persist
- Power management works
- System info accurate

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **Why use SQLite vs flat files?**

   - (Structured, searchable, transactions, better for metadata)

2. **How do you prevent data loss during power-off?**

   - (Sync writes, WAL mode in SQLite, graceful shutdown)

3. **What is CPU governor?**

   - (Policy for CPU frequency scaling, performance vs power)

4. **Why push-to-talk vs auto-detect?**

   - (Deterministic, no false triggers, better for embedded)

5. **How do you calculate audio file size?**

   - (sample_rate × duration × channels × bytes_per_sample)

### Must Be Able to Draw:

```
User: Press Record
    ↓
UI → audio_service: START_RECORD
    ↓
audio_service → ALSA: capture
    ↓
[Recording... timer updates UI]
    ↓
User: Release
    ↓
UI → audio_service: STOP_RECORD
    ↓
audio_service → storage_service: SAVE file.wav
    ↓
UI → stt_service: TRANSCRIBE file.wav
    ↓
stt_service → UI: "transcribed text"
    ↓
UI displays text + edit option
```

---

## ❌ STILL LOCKED:

- System integration
- Performance tuning
- Final optimization

---

# 🟦 LEVEL 7: SYSTEM INTEGRATION

**Theme:** "Everything works together"

## 📚 LEARN (Knowledge Requirements)

### System Architecture

- [ ] **Service dependencies**
- [ ] **Boot sequence** optimization
- [ ] **Error handling** across services
- [ ] **Logging** strategy

### Performance

- [ ] **Profiling** (perf, gprof)
- [ ] **Memory leaks** (valgrind)
- [ ] **CPU utilization**
- [ ] **I/O bottlenecks**

### Reliability

- [ ] **Watchdog** timers
- [ ] **Service recovery**
- [ ] **Data corruption** prevention
- [ ] **Crash reporting**

### Security (Basic)

- [ ] **File permissions**
- [ ] **Service isolation**
- [ ] **No root** for apps
- [ ] **Secure storage**

---

## ✅ DO (Tasks to Complete)

### Task 7.1: Complete System Diagram

```
Document the full architecture:

┌──────────────────────────────┐
│      UI Shell (LVGL)         │
├──────────────────────────────┤
│  Apps: Recorder, Notes       │
├──────────────────────────────┤
│  Services:                   │
│  - audio_service             │
│  - stt_service               │
│  - storage_service           │
│  - ui_service                │
├──────────────────────────────┤
│  Init & Service Manager      │
├──────────────────────────────┤
│  Linux Kernel                │
│  - ALSA drivers              │
│  - Framebuffer               │
│  - Input drivers             │
├──────────────────────────────┤
│  Hardware                    │
└──────────────────────────────┘

For each service:
- Dependencies
- IPC interfaces
- Configuration
- Error handling
```

**Deliverable:**

- Complete architecture doc
- Dependency graph
- IPC protocol specs
- README for each component

---

### Task 7.2: Boot Time Optimization

```bash
# Measure current boot time:
$ systemd-analyze  # If using systemd
# Or manually time init → UI ready

# Optimize:
1. Parallel service startup (where safe)
2. Lazy loading (defer non-critical)
3. Optimize kernel (remove unused drivers)
4. Reduce initramfs size

Target: < 5 seconds to UI
```

**Deliverable:**

- Boot time measured
- Optimizations applied
- < 5 second boot achieved
- Log shows startup sequence

---

### Task 7.3: Error Recovery

```c
// Implement recovery for common failures:

1. Audio device busy:
   - Retry with backoff
   - Notify user
   - Log error

2. STT service crash:
   - Service manager restarts it
   - Pending requests fail gracefully
   - UI shows error, allows retry

3. Storage full:
   - Detect before writing
   - Delete old recordings (with permission)
   - Alert user

4. Low memory:
   - Monitor /proc/meminfo
   - Kill non-critical services
   - Prevent new recordings
```

**Deliverable:**

- All failure modes handled
- System stays stable
- User sees meaningful errors
- Auto-recovery where possible

---

### Task 7.4: Performance Testing

```bash
# Load testing:

Test 1: Continuous recording for 1 hour
- Check memory leaks
- Check CPU usage
- Check for crashes

Test 2: Rapid start/stop cycles
- 100 recordings in 10 minutes
- Check responsiveness
- Check data integrity

Test 3: Concurrent operations
- Record + transcribe previous + UI navigation
- Measure latency
- Check for deadlocks

Tools:
- top / htop (CPU/RAM)
- iotop (I/O)
- valgrind (memory leaks)
- strace (system calls)
```

**Deliverable:**

- All tests pass
- No memory leaks
- CPU < 70% average
- No crashes in 1-hour test

---

## 🔓 UNLOCK CONDITIONS

### Quiz Yourself:

1. **What happens if storage_service crashes?**

   - (Service manager restarts, pending saves fail, users retry)

2. **How do you detect memory leaks?**

   - (Valgrind, monitor RSS over time, check for growth)

3. **Why parallelize service startup?**

   - (Faster boot, better resource utilization)

4. **What is a watchdog?**

   - (Timer that reboots system if not periodically reset, ensures liveness)

5. **How do you profile CPU usage?**

   - (perf, top, /proc/stat, flame graphs)

### Must Be Able to Explain:

```
Failure scenario: Audio device in use

1. audio_service tries to open device → fails
2. Service logs error
3. Retry with exponential backoff (1s, 2s, 4s)
4. After 3 retries, notify UI via IPC
5. UI shows: "Audio unavailable, please wait"
6. When device available, auto-recover
7. User can manually retry
```

---

## ❌ STILL LOCKED:

- Final optimization
- Documentation
- Demo readiness

---

# 🟦 LEVEL 8: POLISH & VALIDATION

**Theme:** "Ship it"

## 📚 LEARN (Knowledge Requirements)

### Production Readiness

- [ ] **Version control** strategy
- [ ] **Release process**
- [ ] **Backup/restore**
- [ ] **Update mechanism**

### Documentation

- [ ] **User guide**
- [ ] **Developer guide**
- [ ] **Architecture overview**
- [ ] **Troubleshooting**

### Testing

- [ ] **Unit tests** (where critical)
- [ ] **Integration tests**
- [ ] **User acceptance testing**
- [ ] **Regression testing**

### Metrics

- [ ] **Boot time**
- [ ] **Latency** (UI, STT)
- [ ] **Memory footprint**
- [ ] **Power consumption**

---

## ✅ DO (Tasks to Complete)

### Task 8.1: End-to-End Demo Script

```
Prepare a 5-minute demo:

1. Boot system (< 5 seconds)
2. Home screen appears
3. Navigate to Voice Recorder
4. Record 10-second message:
   "This is Black Hand OS, a custom Linux phone
    operating system with offline voice-to-text"
5. Stop recording
6. Transcribe
7. Show accurate transcription
8. Save to Notes
9. Navigate to Notes, show saved note
10. Navigate to Settings, show system info

Must work flawlessly 5 times in a row.
```

**Deliverable:**

- Demo script written
- 100% success rate over 5 trials
- Video recording of successful demo
- Able to present confidently

---

### Task 8.2: Performance Benchmark Report

```markdown
# Black Hand OS - Phase 2 Metrics

## Boot Performance

- Kernel boot: 1.2s
- Init + services: 2.3s
- UI ready: 3.8s
- Total: 3.8s ✓ (target: <5s)

## Runtime Performance

- Audio latency: 32ms
- STT processing: 0.4x real-time
- UI responsiveness: 45ms average
- Memory usage: 380 MB (peak: 420 MB)

## Accuracy

- STT word error rate: 8% (short phrases)
- STT word error rate: 12% (long sentences)

## Reliability

- Continuous operation: 24 hours stable
- Crash-free recordings: 1000+ sessions
- Memory leaks: None detected

## Power (estimated)

- Idle: 120 mA
- Recording: 280 mA
- Transcribing: 450 mA
```

**Deliverable:**

- Complete metrics document
- All targets met
- Graphs/charts of performance
- Comparison to Phase 1 (where applicable)

---

### Task 8.3: Documentation Package

```
Required documentation:

1. README.md
   - What is Black Hand OS?
   - Features
   - Hardware requirements
   - Quick start

2. ARCHITECTURE.md
   - System diagram
   - Component descriptions
   - IPC protocols
   - Data flows

3. BUILD.md
   - Build environment setup
   - Buildroot configuration
   - Cross-compilation steps
   - Deployment procedure

4. USER_GUIDE.md
   - How to use recorder
   - How to use notes
   - Settings explained
   - Troubleshooting

5. DEVELOPER_GUIDE.md
   - Adding new services
   - IPC patterns
   - Debugging techniques
   - Code style
```

**Deliverable:**

- All docs complete
- Clear, concise writing
- Examples included
- Someone else can build from your docs

---

### Task 8.4: Reflection & Next Steps

```markdown
# Phase 2 Complete - Reflection

## What I Built

- Custom Linux phone OS
- Offline voice-to-text capability
- Voice recorder with transcription
- Notes app with voice input
- Complete service architecture

## What I Learned

- Embedded Linux internals
- Audio pipeline on Linux (ALSA)
- STT integration (Vosk/Whisper)
- UI frameworks (LVGL/Qt)
- System architecture design
- Performance optimization
- Service-oriented design

## Phase 1 vs Phase 2

Phase 1 taught me:

- Hardware constraints
- Real-time thinking
- Low-level control

Phase 2 taught me:

- System complexity management
- Software architecture
- ML integration
- Product development

## Known Limitations

- STT accuracy varies with accent/noise
- Limited to one language (English)
- No cellular modem integration (yet)
- Battery life not optimized
- No app ecosystem

## If I Were to Continue (Phase 3)

- Custom hardware design
- Modem integration (calls/SMS)
- Multi-language STT
- Better power management
- OTA updates
- Encryption & security

## Career Readiness

With Phase 1 + Phase 2, I can confidently say:

- I understand embedded systems end-to-end
- I can design and implement complex systems
- I can work with Linux, RTOS, and bare metal
- I have practical ML integration experience
- I can ship products
```

**Deliverable:**

- Honest reflection written
- Clear understanding of what was learned
- Articulate career positioning
- Decision on whether to continue to Phase 3 (hardware)

---

## 🔓 UNLOCK CONDITIONS (PHASE 2 COMPLETE)

### Final Demonstration:

Record yourself giving the full demo. You must:

- Boot the system
- Demonstrate voice recording
- Show transcription accuracy
- Navigate the UI smoothly
- Explain the architecture verbally
- **NO stuttering, NO "it usually works", NO excuses**

This demo is your proof.

### Final Quiz:

1. **Why Linux for offline STT but not for Phase 1?**

   - (Phase 1: constraints teach fundamentals; Phase 2: Linux enables complex software, ML models, ecosystem)

2. **What is the role of init in your OS?**

   - (First process, starts services, defines boot flow, represents your OS's identity)

3. **How does your OS differ from Android?**

   - (Simpler, no Google services, privacy-focused, voice-first, minimal apps, but same fundamental architecture)

4. **Could you port this to different hardware?**

   - (Yes, change device tree + drivers, services/apps stay same)

5. **What would you do differently if starting over?**

   - (Answer honestly based on your experience)

### Must Be Able to Say Confidently:

```
"I built Black Hand OS, a custom Linux-based phone operating
system with offline voice-to-text. It boots in under 4 seconds,
has a clean service-oriented architecture, and can transcribe
speech with 90%+ accuracy. I understand embedded Linux deeply
because I started with bare metal RTOS in Phase 1. I'm ready
for embedded systems roles in consumer electronics, automotive,
or IoT."
```

---

## 🏁 CONGRATULATIONS - PHASE 2 COMPLETE

You have:

- ✅ Mastered embedded Linux
- ✅ Integrated ML (STT) on embedded device
- ✅ Built a real product OS
- ✅ Proven end-to-end systems thinking
- ✅ Created a portfolio-worthy project

---

# 📊 PHASE 2 SUMMARY CHECKLIST

```
LEVEL 1: LINUX FOUNDATIONS
□ Dev board booting custom Linux
□ Custom Buildroot image
□ Hello World service
□ Kernel vs userspace understanding

LEVEL 2: CUSTOM INIT & SERVICES
□ Custom init script
□ Service template
□ IPC between services
□ Service manager

LEVEL 3: AUDIO PIPELINE ON LINUX
□ ALSA hardware working
□ ALSA C API recording
□ Audio service (Linux version)
□ Real-time audio processing

LEVEL 4: OFFLINE SPEECH-TO-TEXT
□ STT engine chosen and running
□ Vosk/Whisper integrated
□ STT service with IPC
□ Performance optimized

LEVEL 5: UI FRAMEWORK
□ UI framework chosen (LVGL/Qt)
□ LVGL on framebuffer working
□ Phone UI shell complete
□ UI ↔ service communication

LEVEL 6: PHONE FEATURES
□ Voice recorder app
□ Notes app with voice input
□ Storage service (SQLite)
□ Settings & power management

LEVEL 7: SYSTEM INTEGRATION
□ Complete system diagram
□ Boot time < 5 seconds
□ Error recovery implemented
□ Performance testing passed

LEVEL 8: POLISH & VALIDATION
□ End-to-end demo (5 successful runs)
□ Performance benchmark report
□ Complete documentation
□ Phase 2 reflection written
```

**When all boxes checked → PHASE 2 DONE**

---

# 🎯 WHAT'S NEXT?

You now have three options:

## Option A: Get a Job (Recommended)

You have enough to land an embedded engineer role. Polish your resume, prepare your demo, start applying.

## Option B: Phase 3 (Custom Hardware)

Design custom PCB, integrate modem, manufacture prototype. This is a huge undertaking (6-12 months).

## Option C: Expand Phase 2

- Add more features (calendar, calculator, games)
- Multi-language STT
- Cloud sync
- Better power optimization

**My recommendation:** Option A. Get real-world experience, get paid, then decide if custom hardware is worth pursuing.

---

``
