# GTK-Based Multi-User Terminal Emulator

## 🚀 Project Overview

This terminal application is designed to provide a user-friendly graphical interface for executing shell commands, supporting both standard terminal operations and multi-user simulation. It features a clean implementation of the **MVC (Model-View-Controller)** design pattern and includes several debugging and error-handling mechanisms.

## 🎯 Key Features

- **Graphical Command Execution**: Run commands using a GTK-based GUI.
- **Standard Terminal Support**:
  - Regular commands (`ls`, `echo`, `pwd`, etc.)
  - **Piping** (`ls | grep txt`)
  - **Redirection** (`>`, `>>`)
  - **Command history**
- **Multi-User Simulation**: Opens two terminal windows simultaneously to mimic concurrent users.
- **Error Handling**: Displays messages for malformed commands and syntax errors (e.g., unclosed quotes).
- **Debug Features**: Command logs and histories are preserved to aid development and testing.

## 🧠 Code Structure (MVC Architecture)

```bash
├── controller.c  // Coordinates command input, parsing, execution logic
├── model.c       // Handles command execution and command history
├── view.c        // Manages the GTK-based GUI (input/output areas)
```

### 📁 File Responsibilities

- **controller.c**
  - Parses user input and supports pipes (`|`) and redirection (`>`, `>>`)
  - Executes commands asynchronously and routes output to the view
- **model.c**
  - Executes commands (`model_execute_command`)
  - Manages process tracking and command history
- **view.c**
  - Creates the GTK interface (input box, output area)
  - Displays command results to the user
- **main.c**
  - Launches two windows to simulate User1 and User2

---

## 🧪 Supported Commands

| Type             | Example                                | Description                          |
|------------------|----------------------------------------|--------------------------------------|
| Basic Commands   | `ls`, `echo`, `pwd`                    | Standard shell commands              |
| Piping           | `ls  grep txt`                         | Multiple pipes supported             |
| Redirection      | `echo hello > file.txt`                | Overwrite                            |
| Append           | `echo world >> file.txt`               | Append to file                       |
| Directory Change | `cd`                                   | Change working directory             |
| Editor Launch    | `nano test.txt`                        | Simulates launching nano in VS Code  |
| Custom Commands  | `@msg`                                 | Placeholder for messaging simulation |

---

## ⚙️ Requirements

### Software

- **GTK 3.0** (GUI framework)
- **GCC** (C compiler)
- **Make** (build automation)
- **VS Code** (optional, used to simulate `nano`)

### System

- **OS**: Linux (Ubuntu/Debian) or other Unix-based systems  
  ⚠️ *Windows support requires WSL or similar environment*

---

## 📦 Installation

### 1. Install GTK (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install libgtk-3-dev
```

### 2. Install GCC and Make (if not already installed)

```bash
sudo apt install build-essential
```

---

## ▶️ Run
### Build
```bash
make
```
### Launch
```
./terminal
```
This will open two terminal windows (User1 and User2), each with an input field and output area. Type your commands into the input field and press Enter to execute.

---

## 🐞 Debugging & Error Handling
- Command history is logged and displayed in the terminal for debugging purposes.
- Malformed commands like nonexistentcommand or "unterminated quotes show descriptive error messages.


