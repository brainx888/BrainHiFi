# BrainHiFi – Getting Started

BrainHiFi decodes high-frequency MEG/EEG brain signals on a high-fidelity, subject-specific MRI head model and turns decoded intent into BCI commands for robotic control, using Meta AI (SAM 3.1 segmentation + Muse LLM).

> Research / BCI software. Not a medical device; not for diagnosis or treatment.

---

## 1. Requirements

| Item | Requirement |
|---|---|
| OS | Windows 10/11, 64-bit |
| Runtime | Microsoft Visual C++ 2015–2022 Redistributable (x64), MFC included |
| Hardware | 8 GB RAM minimum (16 GB recommended), DirectX 12 GPU for 3-D views |
| Network | Internet access to `https://api.meta.ai` for AI features |
| Meta AI key | API key from your Meta AI developer account |

## 2. Installation

1. Unzip the BrainHiFi package into a folder, e.g. `C:\BrainHiFi\`.
2. Keep `BrainHiFi.exe` and `BrainHiFiViewDll.dll` (and any other `*.dll` shipped) **in the same folder**.
3. If Windows reports a missing `mfc140u.dll` / `vcruntime140.dll`, install the VC++ 2015–2022 x64 Redistributable.

## 3. Configure the Meta AI API key (once)

The key is never stored in the program or project files. Set it as a user environment variable:

```bat
setx META_AI_API_KEY "LLM_xxxxxxxx_xxxxxxxxxxxxxxxx"
```

Close and reopen BrainHiFi (and any console) so the new variable is picked up.
Optional: `FAL_KEY` enables the alternative fal.ai SAM 3 backend; without it BrainHiFi uses Meta `sam-3.1`.

## 4. Start the software

1. Double-click `BrainHiFi.exe`.
2. The main window opens. Open the **BrainHiFi MSI Viewer** (from the menu/toolbar) – this dialog is provided by `BrainHiFiViewDll`.

## 5. Typical workflow

### Step 1 – Load the head model (MRI)
- Click **Load MRI** and choose the subject's MRI volume file (CAnaData format, or the shared `.anamc` format).
- Axial, coronal and sagittal slices appear.

### Step 2 – Load the brain-signal source map (MEG/EEG)
- Click **Load Source** and choose the source-imaging file (CVolumSourcImag / SAM 3-D format, or the shared `.fmes` format).
- The source activity is overlaid in colour on the MRI slices.

### Step 3 – Explore the activity
- **Cursor / time** slider: step through time points of the source data.
- **Threshold**: hide weak activity; raise it to keep only the strongest sources.
- **Show negative**: also display negative-polarity sources.
- Click on any slice to move the cross-hair; the other two views follow.
- **Reset** returns views and settings to defaults.

### Step 4 – AI segmentation and decoding (Meta AI)
- Type an instruction in the **Prompt** box, e.g.
  `Segment the motor cortex and report the strongest source cluster.`
- Click **Run AI**. BrainHiFi sends the current slice/source image to Meta `sam-3.1` for segmentation and to `muse` for interpretation.
- The response (detected regions, cluster labels, decoded-intent summary) is shown in the **Result** box.

### Step 5 – BCI / robotic control
- The decoded intent (e.g. left / right / grasp / rest) is mapped to robot commands.
- Connect the robot or simulator, select the command mapping, and start the control session.
- Use **Stop** to immediately halt robot motion.

### Step 6 – Save
- Save results/screenshots from the viewer; AI results can be copied from the Result box.

## 6. Troubleshooting

| Symptom | Fix |
|---|---|
| “DLL not found” on start | Put `BrainHiFiViewDll.dll` next to `BrainHiFi.exe`; install VC++ x64 Redistributable. |
| `401 invalid_api_key` | Check `META_AI_API_KEY` (full `LLM_..._...` key), then restart BrainHiFi. |
| `404 model_not_found` | Model not enabled for your key in the Meta AI developer console. |
| “FAL API key is not set” | Ignore unless using fal.ai; set `FAL_KEY` or use the Meta backend. |
| Source not aligned with MRI | Make sure the MRI and source files are from the same subject and coordinate system. |
| Overlay empty | Lower the **Threshold** or enable **Show negative**. |

## 7. Folder contents

```
BrainHiFi\
  BrainHiFi.exe          Main application
  BrainHiFiViewDll.dll   MSI viewer: MRI + source overlay, Meta AI panel
  Readme.md              This guide
  SampleData\            (optional) demo MRI + source files
```
