# BrainHiFi Sample Dataset: Multimodal MRI / MEG Source Imaging

This repository contains sample structural Magnetic Resonance Imaging (MRI) and Magnetoencephalography (MEG) data packages compressed in `.rar` format. Follow the instructions below to extract, verify, and integrate these dataset files into your **BrainHiFi** processing environment.

---

## 📋 Prerequisites

Because `.rar` is a proprietary high-compression format, standard operating system archivers may require third-party tools to unpack the files cleanly.

### Required Extraction Tools
* **Windows:** Install [7-Zip](https://www.7-zip.org/) or [WinRAR](https://www.win-rar.com/).
* **macOS:** Install [The Unarchiver](https://theunarchiver.com/) (available via Mac App Store) or [Keka](https://www.keka.io/).
* **Linux (Ubuntu/Debian):** Install the `unrar` package via terminal:
  ```bash
  sudo apt-get update && sudo apt-get install -y unrar
  ```

---

## 🗂️ Dataset Directory Structure

Once extracted, the dataset is organized in standard multi-fold neuroimaging hierarchy (BIDS-inspired):

```text
dataset_root/
*.amc: anatomical MRI data
*.mes: functional MEG/EEG source data
*.bhv: combination of MEG/EEG source data and MRI data
```

---

## 🚀 Step-by-Step Extraction Guide

### Method A: Using Command Line (Recommended for Linux/macOS)

1. Open your terminal and navigate to the directory where your `.rar` package is downloaded:
   ```bash
   cd /path/to/downloaded_data
   ```

2. Extract the package preserving all nested directory structures:
   ```bash
   # Extract with full directory path (-x)
   unrar x sample_dataset.rar ./sample_dataset/
   ```

### Method B: Using Graphical Interface (Windows/macOS)

1. Right-click the `.rar` file (e.g., `sample_dataset.rar`).
2. Select your extraction software:
   * **7-Zip (Windows):** Hover over *7-Zip* ➔ Select **"Extract to sample_dataset\"**.
   * **WinRAR (Windows):** Click **"Extract to sample_dataset\"**.
   * **The Unarchiver / Keka (macOS):** Double-click the `.rar` file to unpack automatically.

---

## 🔍 Verification & Integration with BrainHiFi

After extraction, ensure that all MRI volumes and MEG streams are intact before initiating AI processing workflows:

1. **Verify File Hashes (Optional):**
   ```bash
   md5sum sample_dataset/sub-01/mri/T1w.nii.gz
   ```
2. **Load into BrainHiFi:**
   Point your `MultiModalDataManager` or MFC UI configuration workspace to the extracted subject directory:
 

---

## 💬 Support & Inquiries

For questions regarding data preprocessing, coordinate co-registration, or pipeline configuration, please open an Issue in the main BrainHiFi repository.