# Lazarus Chess Engine

Lazarus is a UCI-compatible chess engine utilizing advanced search\* algorithms and a neural network (NNUE) for evaluation, designed for high performance\*\*.

*depends on a person's definition of advanced  
**performance is unlikely

Lazarus is heavily based on code written for Lazarus, its predecessor.

### CLI Commands:

Functional UCI implementation with custom commands:
   - **`d`**: Display the current board position.
   - **`move <move>`**: Applies a move in long algebraic notation.
   - **`bulk <depth>`**: Starts a perft test from the current position using bulk counting.
   - **`perft <depth>`**: Performs a perft test from the current position.
   - **`perftsuite <suite>`**: Executes a suite of perft tests*.
   - **`position kiwipete`**: Loads the "Kiwipete" position, commonly used for debugging.
   - **`eval`**: Outputs the evaluation of the current position.
   - **`moves`**: Lists **all moves**\*\* for the current position.
   - **`gamestate`**: Displays the current board state and game metadata.
   - **`incheck`**: States if the current side to move is in check.
   - **`islegal <move>`**: States if a move is legal in the current position.
   - **`keyafter <move>`**: Give the predicted full position hash after a move.
   - **`piececount`**: Displays piece counts for both sides.  
*Uses bulk counting  
\*\*All pseudolegal moves

### Search Algorithm
Lazarus uses the widespread negamax algorithm with alpha/beta pruning. It features various heuristics that are applied depending on the position. Each heuristic is tested to ensure it improves the engine's performance via a sequential probability ratio test (SPRT)

### Evaluation

Lazarus's neural network is trained on billions of positions from selfplay games. It features an efficiently updatable neural network (NNUE) evaluation trained with [bullet](https://github.com/jw1912/bullet) using a (768->1024)x2->1x8 architecture

## Local Builds

### Prerequisites

- **Make**: Uses make to build the executable.
- **Clang++ Compiler**: Requires support for C++20 or later.
- **CPU Architecture**: AVX2 or later is recommended.
- **Neural Network File**: Optionally provide an NNUE and append the make command with `EVALFILE="./your/network/file"`.

### Compilation

1. Clone the repository:

   ```bash
   git clone https://github.com/Quinniboi10/Lazarus.git
   cd Lazarus
   ```

2. Compile using make:

   ```bash
   make -j
   ```

3. Run the engine:

   ```bash
   ./Lazarus
   ```

## Usage

Lazarus uses the UCI protocol but supports custom debugging and testing commands. Compatible with GUIs like Cute Chess, En Croissant, or any UCI-compatible interface. See above commands.

## Configuration

Lazarus supports a handful of customizable options via the `setoption` command:

- `Threads`: Number of threads to use (1 to 2048). Default: 1.
- `Hash`: Configurable hash table size (1 to 524288 MB). Default: 16 MB.
- `Move Overhead`: Adjusts time overhead per move (0 to 1000 ms). Default: 20 ms.
- `EvalFile`: Path to an external network file. Default "internal".
- `UCI_Chess960`: Bool representing FRC/chess 960. Default false.
- `Softnodes`: Bool representing if `go nodes` should be treated as a hard or soft limit. Default false.

## Special Thanks

- **Vast**: Help hunting for bugs and explaining concepts when I was new
- **Ciekce**: Lots of guidance and test NNUEs
- **Shawn\_xu**: Explaining the implementation of NNUEs and many other things
- **A\_randomnoob**: Fixing obvious mistakes and helping me with heuristics
- **Matt**: Providing help and allowing me to use his git and OpenBench instances
- **jw**: Helping me with NNUE training
- All other members of the Stockfish Discord server who have helped develop Lazarus!