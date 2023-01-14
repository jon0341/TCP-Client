# chat-Client
TCP Chat Client program, used with the chat-server program

-program developed for/tested on linux/MacOS. You will need the server's IP Address and the UDP port that the server is listening on to run the program. This port is manually entered when starting the server program.

to compile: gcc -o chatClient chat_client.c DieWithError.c

-run program from CLI: ./chatClient <SERVER IP>  <SERVER UDP PORT NUMBER> 
 
-upon starting, enter a four-digit login ID (eg 0001, 0002, 1111, 9234, etc)

server will take client's address info and login ID, and store it in an array of active users (capacity of 20)

option 1 will allow you to view the current active users also signed in on the application

option 2 will allow you to chat with one of the users
	1) receiving client is given option to accept or decline the message. if declined, both users are returned to the menu
	2) if accepted, the requesting client messages first, and the users can message each other
	3) users must take turns sending messages, max size 200 characters (199 plus null terminator)
	4) to end the chat, either user sends an end of line character (backslash followed by zero): \0
		-must be only character for that final message
	5) after chat ends both users are returned to the menu
	
option 3 will log a user out. The server deletes that user from the user record, and the directory is updated
  



