# C Terminal Chat Application

This is a robust, multi-client terminal-based chat application written in C. It uses `ncurses` for a feature-rich client UI, `pthreads` for concurrent client handling, and a line-based network protocol for stable communication.

The application supports real-time chat, private messaging, persistent history, a live user list, and many advanced UI features.

## Features

* **Multi-Client Server**: Handles multiple simultaneous client connections using `pthreads`.
* **Auto-Shutdown Server**: The server automatically detects when the last client has disconnected and shuts itself down.
* **Ncurses UI**: The client features a three-panel layout (Users, Chat History, Input) that is fully thread-safe and robust against window resizing.
* **Persistent & Private History**: The server logs all public and private messages. New users receive the *public* chat history, but *private* messages are only shown to the sender and recipient, ensuring privacy.
* **User & Private Messaging**: Users can send public messages to all clients or send a private message to a specific user using the `@username <message>` syntax.
* **Live User List**: The "Users" panel updates in real-time. Your own username is highlighted in **green** so you can easily identify yourself.
* **Per-User Colors**: Each user is automatically assigned one of 6 distinct colors (Red, Green, Yellow, Blue, Magenta, Cyan). All their public and private messages appear in their assigned color, making the chat easy to follow.
* **History Scrolling**: You can use the **Arrow Up** and **Arrow Down** keys to scroll through the entire chat history without interrupting your typing or corrupting the display.
* **Robust Input & Network**:
    * **Stable Input**: The client uses a manual `wgetch` loop to handle typing, backspace, and arrow keys, while safely ignoring disruptive mouse wheel clicks or blank "Enter" presses.
    * **Stable Network**: The server and client use a line-based protocol (messages ending in `\n`). This "message framing" prevents network packets from "sticking together" and ensures that messages are never jumbled.

## Files

* `server.c`: The multi-threaded server application. Manages clients, assigns colors, routes messages, and filters/serves chat history.
* `client.c`: The client application. Provides the advanced `ncurses` UI, including the scrollable "pad" for history and the manual input loop.
* `net_helper.c`: A helper library providing simplified functions for socket creation, connection, and I/O, including the `Connection` struct.
* `Makefile`: For easy compilation of the server and client.

## Compilation

A `Makefile` is provided for easy compilation.

1.  **To build both the server and client:**
    ```bash
    make all
    ```
    (or just `make`)

2.  **To clean up build files and the log:**
    ```bash
    make clean
    ```

## How to Use

1.  **Start the Server:**
    First, run the server in a terminal. It will automatically listen on `127.0.0.5` at port `9000`.
    ```bash
    ./server
    ```
    The server will print `Server listening on port 9000...`

2.  **Start the Client(s):**
    Open one or more new terminal windows and run the client.
    ```bash
    ./client
    ```

3.  **Log In:**
    The client will first ask you for a username in the terminal *before* the UI starts.
    ```
    Enter your username: Arefin
    ```
    If the username is valid, the `ncurses` UI will launch.

4.  **Chatting:**
    * **Public Chat:** Type any message and press Enter.
    * **Private Chat:** To send a private message, use the `@username` syntax.
    * **Scroll History:** Use the **Arrow Up** and **Arrow Down** keys.
    * **Quit:** To disconnect and close the client, type `:q` and press Enter.
