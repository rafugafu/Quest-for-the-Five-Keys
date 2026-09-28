# Quest for the Five Keys  
This is a text adventure game originally made in Python, and translated to C++.  
**NOTE: PLEASE DO NOT READ THE CODE BEFORE PLAYING THE GAME, AS YOU WILL SPOIL IT FOR YOURSELF**  
# Game  
## Help and Introduction  
The starting introduction of the game is given here:  
```
You are one of the world's foremost research scientists. After years of work, you had finally completed the greatest experiment of your career.

Before you could present your discovery, your rivals stole the results of your experiment and fled to a remote island. There they took extraordinary measures to ensure no one could recover your work.

The complete result is sealed inside a high-security electronic device of your own making that you were using to store your work. It can only be opened by entering **five different passwords**, each hidden somewhere on the island. Beware! Enter a single incorrect password and the device will destroy itself, taking your experiment with it forever.

The island is inhabited. Its people know nothing of your rivals' actions, but some may help you if you can persuade them, while others may have something you need.

Can you recover the five passwords, unlock the device, and reclaim your stolen work? Your success depends entirely on your ingenuity.

Your fate-and the fate of your experiment-is now in your hands.
```
Further help and tips are given by the game itself while playing.  
## To Play  
To play the game, either run the Python version (no dependencies), or download a compiled C++ binary for your platform on the GitHub [releases](https://github.com/rafugafu/quest-for-the-five-keys/releases).  
The game normally requires a terminal emulator which supports colors and the alt-screen to render colors and the help screen. If you prefer to play without colors, run the game with the `--no-color` option. If you do not have a capable terminal emulator, run with `--no-ansi` to strip all terminal escape codes from the output.
If you want to retain a transcript of your play later, run with the option `--log-file FILENAME` to save a plain-text transcript to FILENAME in the format:
```
=== Game Output ===
...
=== User Input ===
...
```
and so on.  
# Original Python Code  
The Python original is at [Game.py](Game.py).  
# C++ Translation  
A byte-identical-output C++ translation can be found at [Game_translation.cpp](Game_translation.cpp).  
## Compilation  
The game is compiled with `g++ -std=c++17 -Os -flto -s -fno-rtti -fno-exceptions -fdata-sections -ffunction-sections -Wl,--gc-sections -fno-stack-protector -fno-ident -o Game Game.cpp` for maximum efficiency.  