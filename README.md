# Real-Time Voice Keyword Spotting on STM32H523 (Cortex-M33)

Embedded real-time keyword spotting (KWS) system running on an **STM32H523CET6** microcontroller at **250 MHz** under **Keil RTX5** RTOS.  
The firmware continuously captures 16 kHz audio from an **INMP441** I2S MEMS microphone using a **GPDMA Linked-List** double-buffer pipeline, extracts **MFCC** features, runs a quantized `int8` **1D-CNN** model via **TensorFlow Lite for Microcontrollers + CMSIS-NN**, and visualizes live per-frame probabilities and the 1-second winning class on an **SSD1306** OLED display.

**Recognized classes (5):** `five`, `stop`, `zero`, `noise`, `unknow`

---
## Video Demonstration

[![Watch the video](https://img.youtube.com/vi/T0OUtBiwelQ/maxresdefault.jpg)](https://www.youtube.com/watch?v=T0OUtBiwelQ)
---

## Toolchain & Environment

* **Environment:** VS Code (Keil Studio)
* **Toolchain:** Arm Compiler 6.24 (Keil MDK v6)
* **Build System:** CMSIS-Toolbox (`csolution` / `cbuild`)
* **Target MCU:** STM32H523CET6 (LQFP48, Arm Cortex-M33 @ 250 MHz, 512 KB Flash, 272 KB SRAM, hardware FPv5-SP FPU + DSP extensions, ICACHE enabled)
* **Drivers:** STM32H5xx Low-Layer (`LL`) drivers (`USE_FULL_LL_DRIVER`)
* **RTOS:** Keil RTX5 (CMSIS-RTOS2)
* **ML & DSP:** TensorFlow Lite for Microcontrollers (TFLM), CMSIS-NN (`int8` per-channel quantization), CMSIS-DSP (`arm_rfft_fast_f32`, MFCC feature extraction)
* **Audio Sensor:** INMP441 (24-bit I2S MEMS Microphone, configured as 16 kHz `16B_EXTENDED` mono)
* **Display:** SSD1306 128×64 OLED (`I2C1` @ `0x3C`, `Font_6x8` for table/labels, `Font_11x18` for winning class)

---

## Hardware Connections

### INMP441 Microphone (`I2S1` / `SPI1`, Master RX)
| MCU Pin | Function | Notes |
| :--- | :--- | :--- |
| `PA4` | `I2S1_WS` (`AF5`) | Word Select (16 kHz) |
| `PA5` | `I2S1_CK` (`AF5`) | Bit Clock (`1.024 MHz`, `16B_EXTENDED` 32-bit slot) |
| `PA6` | `I2S1_SDI` (`AF5`) | Serial Data In |
| `PA3` | GPIO Output | Microphone `L/R` select (`0` = Left channel) |


### SSD1306 128×64 OLED Display (`I2C1`)
| MCU Pin | Function | Notes |
| :--- | :--- | :--- |
| `PB7` | `I2C1_SDA` (`AF4`) | I2C Serial Data |
| `PB8` | `I2C1_SCL` (`AF4`) | I2C Serial Clock |

---

## System Architecture

### 1. Zero-CPU Audio Capture (`I2S1` + `GPDMA1` Linked-List)
* **`GPDMA1_Channel0`** operates in **2-node circular Linked-List mode** (`Node A -> Node B -> Node A`), transferring 16-bit samples directly from `SPI1->RXDR` into `buffer_dma_A` and `buffer_dma_B`.
* Source reads use **AHB Port 0**, while destination memory writes and Linked-List descriptor fetches use **AHB Port 1** for pipelined bus access.
* Interrupts trigger on `TCEM_EACH_LLITEM_TRANSFER` (after the next node is already loaded into channel registers). The ISR reads `CxLLR` to identify the completed buffer and signals the audio thread via `osThreadFlagsSet()`.

### 2. RTOS Threads (Keil RTX5)

| Thread | Priority | Stack Size | Responsibility |
| :--- | :--- | :--- | :--- |
| `mic_dma` | `osPriorityHigh` | `512 B` | Extracts selected mono channel, runs Q8 DC-blocker + gain + `__SSAT` clipping protection, writes into a 16,000-sample circular window, and triggers inference every hop (`AI_WIN_LEN / AI_HOPS_PER_SEC`). |
| `ai_infer` | `osPriorityNormal` | `2048 B` | Wakes on hop flag, computes MFCC features and runs `int8` 1D-CNN inference on an isolated 1-second snapshot buffer (`s_infer_buf`). |
| `oled_ui` | `osPriorityBelowNormal` | `1024 B` | Aggregates the last `AI_HOPS_PER_SEC` inference frames using time-weighted scoring, draws probability history and the winning class onto the SSD1306 framebuffer. |

---

## Audio Front-End & DSP Pipeline (MFCC)

* **Sample Rate:** `16 kHz` (1.0 s sliding window = `16,000` samples)
* **Hop Size:** configurable via `AI_HOPS_PER_SEC` (e.g., `4` $\rightarrow$ `250 ms` / 4 inferences per second, `2` $\rightarrow$ `500 ms`)
* **Pre-emphasis coefficient:** `0.98`
* **MFCC parameters:**
  * Number of coefficients: `13`
  * Frame length: `25 ms` (`0.025 s`)
  * Frame stride: `20 ms` (`0.020 s`) $\rightarrow$ `49` time frames per 1 s window (`49 × 13 = 637` features)
  * Mel filterbanks: `32`
  * FFT length: `512`
  * Normalization window size: `151`
  * Low frequency cutoff: `80 Hz`

---

## Neural Network Model & Training

A lightweight **1D-CNN** architecture is used instead of a 2D-CNN, reducing RAM and CPU usage by ~10–12× while maintaining high accuracy on 1-second speech commands:

* **Input Reshape:** `(49, 13)`
* **Block 1:** `Conv1D(24, kernel=3, same, L2=1e-4)` $\rightarrow$ `BatchNorm` $\rightarrow$ `ReLU` $\rightarrow$ `MaxPool1D(2)` $\rightarrow$ `Dropout(0.15)`
* **Block 2:** `Conv1D(32, kernel=3, same, L2=1e-4)` $\rightarrow$ `BatchNorm` $\rightarrow$ `ReLU` $\rightarrow$ `MaxPool1D(2)` $\rightarrow$ `Dropout(0.20)`
* **Block 3:** `Conv1D(48, kernel=3, same, L2=1e-4)` $\rightarrow$ `BatchNorm` $\rightarrow$ `ReLU` $\rightarrow$ `MaxPool1D(2)` $\rightarrow$ `Dropout(0.25)`
* **Classifier Head:** `Flatten` $\rightarrow$ `Dense(32, L2=1e-4)` $\rightarrow$ `BatchNorm` $\rightarrow$ `ReLU` $\rightarrow$ `Dropout(0.30)` $\rightarrow$ `Dense(5, softmax)`

**Training Configuration:**
* **Augmentation:** `SpecAugment` (1 time mask, max 3 consecutive frames, mean-filled)
* **Loss & Regularization:** `CategoricalCrossentropy(label_smoothing=0.1)`, weight decay `1e-4`
* **Optimizer:** `Adam` (`lr = 0.001`), `batch_size = 64`, up to `80` epochs
* **Callbacks:** `ReduceLROnPlateau(factor=0.5, patience=5, min_lr=1e-5)`, `EarlyStopping(patience=15, restore_best_weights=True)`
* **Quantization:** Full `int8` per-channel quantization (`disable_per_channel_quantization = False`)

---

## Real-Time Performance (STM32H523 @ 250 MHz)

Measured execution time per 1-second window (`16,000` samples):
* **DSP (MFCC extraction):** `43–44 ms`
* **Neural Network (`int8` 1D-CNN):** `3 ms`
* **Total Inference Latency:** **`~46–47 ms`** (~18.8% CPU load at 4 inferences/sec, `0` skipped hops)

*(Microsecond profiling is handled via Cortex-M33 `DWT->CYCCNT`; millisecond OS timing uses `osKernelGetTickCount()`).*

---

## Post-Processing & OLED Display Layout

### Winner Selection Logic (`ai_oled_shared.h`)
* Only target commands (`five`, `stop`, `zero`) compete to trigger a command detection.
* Each frame in the 1-second history buffer is weighted by recency (`AI_FRAME_DECAY_STEP = 0.20f`, giving `[0.4, 0.6, 0.8, 1.0]` for 4 frames/sec) and normalized to `[0.0, 1.0]`.
* A target command wins if it satisfies both thresholds:
  * **Single-frame peak:** `peak >= AI_CMD_PEAK_THRESH` (`0.55f`)
  * **Time-weighted score:** `weighted_score >= AI_CMD_WEIGHT_THRESH` (`0.28f`)
* If no target command meets both thresholds, the system falls back to `NOISE` or `UNKNOW` (whichever has the higher weighted score).
* The percentage displayed next to the winner reflects the **peak single-frame confidence** within the current 1-second history window.

### SSD1306 Screen Layout (`128 × 64`)
* **Rows `y = 0..31`:** Up to `AI_HOPS_PER_SEC` (2 or 4) rows of per-frame probabilities above each class column (`Font_6x8`).
* **Row `y = 35..42`:** Class headers (`FIVE`, `NOIS`, `STOP`, `UNKW`, `ZERO`).
* **Row `y = 46..63`:** Winning class name and peak probability (`Font_11x18`, e.g. `ZERO 94%`).

---

## Build Notes

* **Heap Size:** Increase `Heap_Size` in `startup_stm32h523xx.s` (e.g. `0x8000` / 32 KB) to accommodate DSP/FFT temporary allocations.
* **Compiler Flags (AC6):** `-std=gnu11` (C), `-Wno-macro-redefined`, `-Wno-pragma-pack`, `-Wno-parentheses-equality`, `-Wno-license-management`, hardware FPU enabled (`Single Precision`), and `ARM_MATH_CM33` defined for CMSIS-DSP/CMSIS-NN.
* **Clock Configuration:** Define `HSE_VALUE` globally in the project configuration to match the actual external crystal frequency (`8 MHz` in this setup). The STM32 LL clock initialization routines rely on this macro for PLL frequency calculations. An incorrect or default value leads to incorrect clock configuration and prevents `PLL2P` from generating the required exact `16 kHz` `I2S1` word-select frequency for the INMP441 microphone.