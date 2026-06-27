# IOCTL-Bridge

A lightweight, educational Proof-of-Concept (PoC) demonstrating low-level process memory analysis in Windows. This project explores safe and high-performance inter-process communication (IPC) between a Windows Kernel-Mode Driver and a User-Mode Application.

## 🚀 Features
* **Kernel-Level Access:** Utilizes a custom Windows driver to safely handle process contexts.
* **IOCTL Communication:** Implements custom I/O Control codes for robust command dispatching.
* **Shared Memory (Mapped Sections):** Uses Shared Memory for ultra-low latency, zero-copy data transfer between ring 0 and ring 3.
* **Target:** Specifically tailored for parsing local memory structures of the single-player game *Skul: The Hero Slayer*.

## ⚠️ Disclaimer
This project is developed strictly for educational, research, and reverse-engineering portfolio purposes. It does not target multiplayer environments or bypass commercial anti-cheat solutions. Use responsibly.
