#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int port = 3000;
int listenfd;

#define MAXNAME 80  /* maximum permitted name size, not including \0 */

#define NPITS 6  /* number of pits on a side, not including the end pit */

struct player {
    int fd;
    int pits[NPITS+1];
    int finishread;             // 0 represent haven't read the name, 1 stand for finish reading but unchecked, 2 stand for valid name
    int namesize;               // this variable stand for the length of the name
    char name[MAXNAME+2];       // this char array is used to store the name, the extra 2 is used for \r\n
    struct player *next;
    struct sockaddr_in q;       // this variable is used to print the IP number
} *playerlist = NULL;

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s, int specialfd);         // I modify this function and add a specialfd argument to it. So this function will 
                                                       // send message to all players instead of the player with specialfd
                                                       

int main(int argc, char **argv)
{
    struct player *p, *new, *check;                 // *p, *check is used to trace the turn and *new is used to create new player
    struct player *curplayer = NULL;                // this pointer points to the current player 
    struct player **pp;                             // this pointer is used to delete nodes
    char msg[MAXNAME + 50];                         // msg is used to store the message which will be sent to players
    char buf[MAXNAME + 2];                          // this buf is used for reading information from the pipe
    char *newline_n;                                // this pointer is used for finding the \n
    char *newline_r;                                // this pointer is used for finding the \r
    int newfd, maxfd;                               // newfd is used store the fd of newplayer
    fd_set fds;                     
    int len;                                        
    int startpebble=4;      
    int move;                                       // move is used for storing the pits number that is entered by player
    struct sockaddr_in q;
    socklen_t q_len;

    parseargs(argc, argv);
    makelistener();

    while (!game_is_over()) {
        FD_ZERO(&fds);                             // here initialize the fds and add listendfd to it
        FD_SET(listenfd, &fds);
        maxfd = listenfd;
        for(p=playerlist; p; p=p->next){           // here reset the maxfd add all players to the listen side to select
            FD_SET(p->fd, &fds);
            if (p->fd > maxfd)
            {
                maxfd = p->fd;
            }
        }
        switch (select(maxfd + 1, &fds, NULL, NULL, NULL)){
        case -1:
            perror("select");
            return 1;
        default:
            // if a new player connects
			// then print the hint message at the screen of the new player, show connection
			// on the server side and create a new player structure and add it to the playerlist
            if (FD_ISSET(listenfd, &fds)){
                // accept the connection from the new player
                q_len = sizeof q;
                if((newfd = accept(listenfd, (struct sockaddr *)&q, &q_len)) < 0){
                    perror("accept");
                    return(1);
                }
                //put it into the fds to make select listen to it
                FD_SET(newfd, &fds);
                if(newfd > maxfd){
                    maxfd = newfd;
                }
                
                //print some information to the server and screen of new player
                printf("connection from %s\n", inet_ntoa(q.sin_addr));
                write(newfd, "Welcome to Mancala.  What is your name?\r\n", 41);
                
                // create a new player structure and intialize the information of new player
				// add the new player to the beginning of the playerlist
                if((new = malloc(sizeof(struct player))) == NULL){
                        fprintf(stderr, "out of memory!\n");
                        exit(1);
                }
                new->finishread=0;              // because the player have not read his name, so set it 0
                new->fd = newfd;                // set its fd to the newfd
                new->name[0] = '\0';            // intialize his name
                new->q = q;                     // used for printing the IP address
                new->namesize = 0;              // because we have not read the name, namesize is 0
                startpebble = compute_average_pebbles();    // get the number of pebbels in each pit
                if(playerlist){                             // add the new player to the playerlist
                    new->next = playerlist;
                    playerlist = new;
                }else{
                    new->next = NULL;
                    playerlist = new;
                }
                
                for(int i=0; i<NPITS; i++){                 // intialize its pits
                    (new->pits)[i] = startpebble;
                }
                (new->pits)[NPITS] = 0;
            // if the information is from a player in playerlist, then first find out who he is 
            // then read information from his pipe and deal with it according to certain situation
            } else {
                // loop around the players to find out who is giving information
                for(p = playerlist; p; p = p->next){
                    if(FD_ISSET(p->fd, &fds)){
                        // now I have already find person, then read information from the pipe
                        len = read(p->fd, buf, sizeof(buf) - 1);
                        // if len is 0, it means the player drops the connections
                        // if one player drops the connection, then close his fd, remove it from the linked list.
                        // print the message to all the players and server that someone has left.
                        if(len == 0){
                            // close the fd of the player
                            close(p->fd);
                            FD_CLR(p->fd, &fds);
                            // print information to the server and all players
                            printf("disconnecting client %s\n", inet_ntoa((p->q).sin_addr));
                            snprintf(msg, sizeof(msg), "%s has left the game\r\n", p->name);
                            broadcast(msg, p->fd);
                            // if it is the turn of person who drops the connection
                            // then print the board to all players and past turn to the next player
                            if(p == curplayer){
                                // print the board
                                check = playerlist;
                                for(check = playerlist; check; check =check->next){
                                    if(check->namesize != 0 && check != p){
                                        snprintf(msg, sizeof msg, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [end pit]%d\r\n", check->name, check->pits[0], check->pits[1], check->pits[2], check->pits[3], check->pits[4], check->pits[5], check->pits[6]);
                                        broadcast(msg, p->fd);
                                    }
                                }
                                // pass the turn to next player
                                if(curplayer->next){
                                    curplayer = curplayer->next;
                                    write(curplayer->fd, "Your move?\r\n", 12);
                                    snprintf(msg, sizeof(msg), "It is %s's move.\r\n", curplayer->name);
                                    broadcast(msg, curplayer->fd);
                                }else if(p == playerlist){
                                    curplayer = NULL;
                                    printf("No one's turn\n");
                                }else{
                                    curplayer = playerlist;
                                    write(curplayer->fd, "Your move?\r\n", 12);
                                    snprintf(msg, sizeof(msg), "It is %s's move.\r\n", curplayer->name);
                                    broadcast(msg, curplayer->fd);
                                }
                            }
                            // remove the player from the playerlist
                            for (pp=&playerlist; *pp && (*pp)->fd != p->fd; pp = &(*pp)->next);
                            if(*pp && (*pp)->fd == p->fd){
                                struct player *next = (*pp)->next;
                                free(*pp);
                                *pp = next;
                            }
                        // if len is smaller than 0, then we meet some error with read
						// then print some error message here
                        } else if(len < 0){
                            perror("read");
                            return(1);
                        // if len is greater than 0, then the information that we get 
						// some one's move or name, find out which situation it is 
                        // and deal with two different situations accordingly
                        } else {
                            buf[len] = '\0';
                            // if the player haven't got a name, then it means the information that we get
							// is the player's name, then add the information to end of the name attribute of the player
                            if(p->finishread == 0){
                                // check the size of the name, if exceed the array cound, then disconnect the player
                                p->namesize += strlen(buf);
                                if(p->namesize >= MAXNAME + 2){
                                    // clear the fd and print message to all players and server
                                    close(p->fd);
                                    FD_CLR(p->fd, &fds);
                                    printf("disconnecting client %s\n", inet_ntoa((p->q).sin_addr));
                                    snprintf(msg, sizeof(msg), "%s has left the game\r\n", p->name);
                                    broadcast(msg, p->fd);
                                    // if now is in the turn of disconnecting player
                                    // then print the board , modify the turn and curplayer
                                    if(p == curplayer){
                                        check = playerlist;
                                        for(check = playerlist; check; check =check->next){
                                            if(check->namesize != 0 && check != p){
                                                snprintf(msg, sizeof msg, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [end pit]%d\r\n", check->name, check->pits[0], check->pits[1], check->pits[2], check->pits[3], check->pits[4], check->pits[5], check->pits[6]);
                                                broadcast(msg, p->fd);
                                            }
                                        }
                                        if(curplayer->next){
                                            curplayer = curplayer->next;
                                            write(curplayer->fd, "Your move?\r\n", 12);
                                            snprintf(msg, sizeof(msg), "It is %s's move.\r\n", curplayer->name);
                                            broadcast(msg, curplayer->fd);
                                        }else if(p == playerlist){
                                            curplayer = NULL;
                                            printf("No one's turn\n");
                                        }else{
                                            curplayer = playerlist;
                                            write(curplayer->fd, "Your move?\r\n", 12);
                                            snprintf(msg, sizeof(msg), "It is %s's move.\r\n", curplayer->name);
                                            broadcast(msg, curplayer->fd);
                                        }
                                    }
                                    // remove the player from the playerlist
                                    for (pp=&playerlist; *pp && (*pp)->fd != p->fd; pp = &(*pp)->next);
                                    if(*pp && (*pp)->fd == p->fd){
                                        struct player *next = (*pp)->next;
                                        free(*pp);
                                        *pp = next;
                                    }
                                // if the namesize is appropriate, then add it to the player's name
                                } else {
                                    strcat(p->name, buf);
                                    buf[0] = '\0'; 
                                }
                                // check whether the player has finished entering his name which is
								// showed by \r\n from the player side
                                // if he has already entered his whole name, then modify
                                // the finishread attribute of the player to 1 which shows his name
                                // has finished reading
                                newline_r = strchr(p->name, '\r');
                                newline_n = strchr(p->name, '\n');
                                if(newline_r){
                                    p->finishread = 1;
                                    *newline_r = '\0';
                                }
                                if(newline_n){
                                    p->finishread = 1;
                                    *newline_n = '\0';
                                }
								
                                // check the validity of the player's name here
                                if(p->finishread == 1){
                                    // if the user enter a empty name, then let he reenters his name
                                    if(strlen(p->name) == 0){
                                        printf("rejecting empty name from %s\n", inet_ntoa((p->q).sin_addr));
                                        write(p->fd, "What is your name?\r\n", 20);
                                        p->finishread = 0;
                                        p->namesize = 0;
                                        p->name[0] = '\0';
                                    }
                                    // check if someone else has already used this name
                                    check = playerlist;
                                    while(check){
                                        if((check->finishread == 2) && (strcmp(p->name, check->name) == 0)){
                                            printf("rejecting duplicate name '%s' from %s\n", p->name, inet_ntoa((p->q).sin_addr));
                                            write(p->fd, "Sorry, someone else already has that name.  Please choose another.\r\n", 68);
                                            (p->name)[0] = '\0';
                                            p->finishread = 0;
                                            p->namesize = 0;
                                            break;
                                        }
                                        check = check->next;
                                    }
                                }
                                
                                // if the finishread attribute doesn't change to 0 after checking
                                // then it means we have got the name and the name is valid
								//  so change this attribute finishread to 2 which means the name which have
								// got is a complete and valid name
                                if(p->finishread == 1){
                                    p->finishread = 2;
                                    // because we have got the name, then we can announce the player has enter the name
                                    snprintf(msg, sizeof msg, "%s has joined the game.\r\n", p->name);
                                    broadcast(msg, -1);
                                    printf("%s's name is set to %s\n", inet_ntoa((p->q).sin_addr), p->name);
                                    check = playerlist;
                                    // print the board for the new player
                                    for(check = playerlist; check; check =check->next){
                                        if(check->namesize != 0){
                                            snprintf(msg, sizeof msg, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [end pit]%d\r\n", check->name, check->pits[0], check->pits[1], check->pits[2], check->pits[3], check->pits[4], check->pits[5], check->pits[6]);
                                            write(p->fd, msg, strlen(msg));
                                        }
                                    }
                                    // if he is not the first player to enter the game, then tell him whose turn it is 
                                    if (curplayer)
                                    {
                                        snprintf(msg, sizeof msg, "It is %s's turn.\r\n", curplayer->name);
                                        write(p->fd, msg, strlen(msg));   
                                    // if he is the first player, then tell him to move
                                    }else{
                                        write(p->fd, "Your move?\r\n", 12);
                                        curplayer = p;  
                                    }
                                }
                            // if what we gets is the pit number
							// then check whether the pit number is valid
							// if the pit number is valid, then do the move by the pit number
							// I store the pit number in the move attribute
                            } else if(isdigit(buf[0])) {
                                // first check whehter is this person's turn
                                // if not, print the message
                                if(p != curplayer){
                                    write(p->fd, "It is not your move.\r\n", 22);
                                // if the person follow the right order of turn
                                // then do the move by the given pit number
                                } else {
                                    // check whether the move is valid
                                    // a move is valid if the pit number that we get is 0, 1, 2, 3, 4, 5
                                    move = atoi(buf);
                                    if(move >= 0 && move <= 5){
                                        // if it is a valid move, then move the pebbles in this player's pits
                                        // and print the move message to all the players
                                        int pebble = p->pits[move];
                                        p->pits[move] = 0;
                                        snprintf(msg, sizeof msg, "You take %d pebbles from pit %d.\r\n", pebble, move);
                                        write(p->fd, msg, strlen(msg));
                                        snprintf(msg, sizeof msg, "%s takes %d pebbles from pit %d.\r\n", p->name, pebble, move);
                                        broadcast(msg, p->fd);
                                        printf("%s takes %d pebbles from pit %d\n", p->name, pebble, move);
                                        check=p;
                                        // here move the pebbles
                                        while(pebble>0){
                                            move+=1;
                                            if(move == 7){
                                                move = 0;
                                                check = check->next;
                                                if(!check){
                                                    check = playerlist;
                                                }
                                            }
                                            check->pits[move] += 1;
                                            pebble-=1;
                                        }
                                        // if the person land on the his own endpit, then let him play again
                                        // and print message to all other players and server
                                        if(move == 6 && check == p){
                                            write(p->fd, "You put the last pebble in your end pit, so you get to go again.\r\n", 68);
                                            snprintf(msg, sizeof msg, "%s put the last pebble in their end pit, so they get to go again.\r\n", p->name);
                                            broadcast(msg, p->fd);
                                            printf("%s put the last pebble in their end pit, so they get to go again.\n", p->name);
                                            check = playerlist;
                                            for(check = playerlist; check; check =check->next){
                                                if(check->namesize != 0){
                                                    snprintf(msg, sizeof msg, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [end pit]%d\r\n", check->name, check->pits[0], check->pits[1], check->pits[2], check->pits[3], check->pits[4], check->pits[5], check->pits[6]);
                                                    write(p->fd, msg, strlen(msg));
                                                }
                                            }
                                            write(p->fd, "Your move?\r\n", 12);
                                        // if the player finished his move, then change it to the next player
                                        } else {
                                            if(p->next){
                                                curplayer = p->next;
                                            }else{
                                                curplayer = playerlist;
                                            }
                                            for(check = playerlist; check; check =check->next){
                                                if(check->namesize != 0){
                                                    snprintf(msg, sizeof msg, "%s: [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d [end pit]%d\r\n", check->name, check->pits[0], check->pits[1], check->pits[2], check->pits[3], check->pits[4], check->pits[5], check->pits[6]);
                                                    broadcast(msg, -1);
                                                }
                                            }
                                            write(curplayer->fd, "Your move?\r\n", 12);
                                            snprintf(msg, sizeof msg, "It is %s's move.\r\n", curplayer->name);
                                            broadcast(msg, curplayer->fd);
                                            printf("%s's turn\n", curplayer->name);
                                        }
                                    // if it is not a valid move, then print the message to the player to move again
                                    } else {
                                        write(p->fd, "Pit numbers you can play go from 0 to 5.  Try again.\r\n", 54);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // if the game finishes, then make a conlusion on all players' points
    close(listenfd);
    broadcast("Game over!\r\n", -1);
    printf("Game over!\n");
    for (p = playerlist; p; p = p->next) {
	int points, i;
	for (points = i = 0; i <= NPITS; i++)
	    points += p->pits[i];
	printf("%s has %d points\r\n", p->name, points);
	snprintf(msg, sizeof msg, "%s has %d points\r\n", p->name, points);
	broadcast(msg, -1);
    }

    return(0);
}

// this function is used to get the port number here, the port number is not given by the server,
// then the default port is 3000
void parseargs(int argc, char **argv)
{
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
	switch (c) {
	case 'p':
	    port = atoi(optarg);
	    break;
	default:
	    status++;
	}
    }
    if (status || optind != argc) {
	fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
	exit(1);
    }
}

// this function set the socket to listen on connection
void makelistener()
{
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	perror("socket");
	exit(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
	perror("bind");
	exit(1);
    };

    if (listen(listenfd, 5)) {
	perror("listen");
	exit(1);
    }
}




int compute_average_pebbles()  /* call this BEFORE linking the new player in to the list */
{
    struct player *p;
    int i;

    if (playerlist == NULL)
	return(4);

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
	nplayers++;
	for (i = 0; i < NPITS; i++)
	    npebbles += p->pits[i];
    }
    return((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() /* boolean */
{
    struct player *p;
    int i;
    if (!playerlist){
	return(0);  /* we haven't even started yet! */
	}
    for (p = playerlist; p; p = p->next) {
	int is_all_empty = 1;
	for (i = 0; i < NPITS; i++)
	    if (p->pits[i]){
		is_all_empty = 0;
		}
	if (is_all_empty)
	    return(1);
    }
    return(0);
}

// this function is used to print message to all players instead of the player who has the special id
void broadcast(char *s, int specialfd){
    struct player *p;
    for(p=playerlist; p; p=p->next){
        if(((specialfd == -1) || (p->fd != specialfd)) && (p->fd != listenfd)){
            write(p->fd, s, strlen(s));
        }
    }
}