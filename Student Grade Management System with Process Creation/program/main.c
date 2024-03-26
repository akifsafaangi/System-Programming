#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>
#include <sys/wait.h>
#include <time.h>

#define MAX_SIZE 100

char *fileName;
int txtFile;

//Signal flag
int sigInt = 0;


/* Signal handler function */
void handler(int signal_number);

/* It checks the given input and determines the command. It checks whether the entered input is valid according to the command. Returns int according to command */
int checkCommand(int argc, char parameters[][MAX_SIZE]);

/* Create a file or discard previous content of a file according to filename */
void gtuStudentGrades();

/* Function to add student grade to the file */
void addStudentGrade(int argc, char parameters[][MAX_SIZE]);

/* Check if grade's size is 2 */
int isValidGrade(const char grade[]);

/* It opens the file entered at input, searches for the entered student, and prints it to the terminal if it exists */
void searchStudent(int argc, char parameters[][MAX_SIZE]);

/* Sorts the students i the file. It may sort by student name or grade, in ascending or descending order. Prints the students */
void sortAll(int argc, char parameters[][MAX_SIZE]);

/* Function to compare two lines based on name */
int compareLinesWithName(const void *a, const void *b);

/* Function to compare two lines based on grade */
int compareLinesWithGrade(const void *a, const void *b);

/* According to command it may prints all entries, first 5 entries or entries within a certain range */
void listEntries(int numOfEntries, int pageNumber);

/* Checks if the given chars are digit or not */
int checkDigit(char token[MAX_SIZE]);

/* Saves the current operation to log file */
void saveLog (char *errorLog);

/* Function to get local time */
char* get_timestamp();

/* Main function to get input and create child processes */
int main(int argc, char *argv[])
{
    if(argc != 1) {
        printf("Usage: ./<filename>\n");
        exit(-1);
    }
    struct sigaction intAct = {0};
    intAct.sa_handler = &handler;
    if((sigemptyset(&intAct.sa_mask) == -1) || sigaction(SIGINT, &intAct, NULL) == -1) {
        perror("Failed to install SIGINT signal handler");
        exit(-1);
    }

    char tokens[MAX_SIZE][MAX_SIZE];
    char buffer[MAX_SIZE];
    int count;
    int childProcesses = 0;
    while(1) {
        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            exit(-1);
        }

        // Clear tokens and buffer
        memset(tokens, 0, sizeof(tokens));
        memset(buffer, 0, sizeof(buffer));
        count = 0;
        ssize_t bytesread;
        bytesread = read(STDIN_FILENO, buffer, MAX_SIZE);
        if(bytesread < 0) {
            perror("Cannot get input from user");
            exit(-1);
        }
            buffer[bytesread] = '\0'; // Null-terminate the buffer
            // Tokenize the input buffer
            char *token = strtok(buffer, " \n\0");
            while (token != NULL) {
                strcpy(tokens[count], token);
                count++;
                token = strtok(NULL, " \n\0");
            }
        if(strcasecmp(tokens[0], "exit") == 0) {
            break;
        }
        int p = checkCommand(count, tokens); // Call checkCommand function to determine which command the user entered.
        if(p == -1) {
            continue;
        }
        pid_t pid = fork(); // Create new process
        if(pid == -1) {
            perror("Fork failed");
            exit(1);
        } else if(pid == 0) { // If process is child. Call the specific function according to p
            fileName = tokens[count-1]; // Sets the name of the file to process.
            if(p == 1) {
                gtuStudentGrades();
            } else if (p == 2) {
                addStudentGrade(count, tokens);
            } else if (p == 3) {
                searchStudent(count, tokens);
            } else if (p == 4) {
                sortAll(count, tokens);
            } else if (p == 5) {
                listEntries(-1, 1);
            } else if (p == 6) {
                listEntries(5, 1);
            } else if (p == 7) {
                listEntries(atoi(tokens[1]), atoi(tokens[2]));
            }
            exit(EXIT_SUCCESS);
        } else {
            childProcesses++;
        }
        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            exit(-1);
        }
    }
    for(int i = 0;i < childProcesses;i++) {
        int r = waitpid(-1, NULL, 0); // Making sure all child processes are finished with their job
        if(r<=0)
        {
            printf("Wait failed.");
            exit(-1);
        }
    }
}

/* Signal handler function */
void handler(int signal_number) {
    sigInt = 1;
}

/* It checks the given input and determines the command. It checks whether the entered input is valid according to the command. Returns int according to command */
int checkCommand(int argc, char tokens[][MAX_SIZE]) {
    if(strcmp(tokens[0], "gtuStudentGrades") == 0) {
        if(argc == 1) { // Shows the commands can be used in program
            printf("Usage: <command>\n");
            printf("Commands:\n");
            printf("1. gtuStudentGrades <filename>\n");
            printf("2. addStudentGrade <name> <surname> <grade> <filename>\n");
            printf("3. searchStudent <name> <surname> <filename>\n");
            printf("4. sortAll <sort-type> <which-order> <filename>\n");
            printf("5. showAll <filename>\n");
            printf("6. listGrades <filename>\n");
            printf("7. listSome <numofEntries> <pageNumber> <filename>\n");
            saveLog("gtuStudentGrades command executed. Commands that can be used are printed\n");
            return -1;
        }
        if(argc != 2) { // If parameters length is not correct
            printf("Usage: gtuStudentGrades <filename>\n");
            return -1;
        }
        return 1;
    } else if(strcmp(tokens[0], "addStudentGrade") == 0) {
        if(argc < 5) { // If parameters length is not correct
            printf("Usage: addStudentGrade <name> <surname> <grade> <filename>\n");
            return -1;
        }
        if(!isValidGrade(tokens[argc-2])) { // If grade's length is not 2 and not in uppercase
            printf("Invalid Grade\n");
            printf("Grade's length must be 2 and uppercase\n");
            printf("Usage: addStudentGrade <name> <surname> AA <filename>\n");
            return -1;
        }
        return 2;
    }   else if(strcmp(tokens[0], "searchStudent") == 0) {
        if(argc < 4) { // If parameters length is not correct
            printf("Usage: searchStudent <name> <surname> <filename>\n");
            return -1;
        }
        return 3;
    }  else if(strcmp(tokens[0], "sortAll") == 0) {
        if(argc != 4) { // If parameters length is not correct
            printf("Usage: sortAll <sort-type> <which-order> <filename>\n");
            return -1;
        }
        return 4;
    }   else if(strcmp(tokens[0], "showAll") == 0) {
        if(argc != 2) { // If parameters length is not correct
            printf("Usage: showAll <filename>\n");
            return -1;
        }
        return 5;
    }   else if(strcmp(tokens[0], "listGrades") == 0) {
        if(argc != 2) { // If parameters length is not correct
            printf("Usage: listGrades <filename>\n");
            return -1;
        }
        return 6;
    }   else if(strcmp(tokens[0], "listSome") == 0) {
        if(argc != 4 || !checkDigit(tokens[1]) || !checkDigit(tokens[2])) { // If parameters length is not correct and int parameters are not int or less than 0
            printf("Usage: listSome <numofEntries> <pageNumber> <filename>\n");
            return -1;
        }
        if(atoi(tokens[2]) == 0 || atoi(tokens[2]) == 0) { // If int parameters are not greater than zero
            printf("Should enter greater than zero\n");
            return -1;
        }
        return 7;
    }  else {
        printf("Invalid command: %s\n", tokens[0]);
        return -1;
    }
    if(sigInt==1)
    {
        printf("SIGINT caught by: %d\n", getpid());
        exit(-1);
    }
}

/* Create a file or discard previous content of a file according to filename */
void gtuStudentGrades() {
    txtFile = open(fileName, O_RDONLY | O_TRUNC | O_CREAT, 0777); // Creates the file or discard content of the file if already exist.
    if(txtFile == -1) {
        perror("The file cannot be opened");
        exit(-1);
    }
    while(close(txtFile) == -1) ;
    // Define a buffer to hold the merged string
    char merged[100];
    // Merge the strings
    strcpy(merged, "gtuStudentGrades command executed to create a file. ");
    strcat(merged, fileName); // Concatenate pointer to merged
    strcat(merged, " file has created succesfully\n");
    saveLog(merged);
    exit(EXIT_SUCCESS);
}


/* Function to add student grade to the file */
void addStudentGrade(int argc, char parameters[][MAX_SIZE]) {
    txtFile = open(fileName, O_WRONLY | O_APPEND, 0333); // Opens the file as write only and append to end of file
    if(txtFile == -1) {
        perror("The file cannot be opened");
        exit(-1);
    }
    // Locks the file to prevent operations on the file at the same time as other operations
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));


    if(sigInt==1)
    {
        printf("SIGINT caught by: %d\n", getpid());
        // Unlocks the file before exit to not block other proceses' operations
        lock.l_type=F_UNLCK;
        while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
        while(close(txtFile) == -1) ; // Close the file
        exit(-1);
    }
    int byteswritten;
    
    int i = 1;
    size_t len = 0;
    // Loops for all contents in the parameters and find the length of complete string
    while(i != argc-1) {
        len += strlen(parameters[i]) + 1; // +1 for space
        i++;
    }
    len++; // For "," between surname and grade
    char *combinedStr = malloc(sizeof *combinedStr * len); // Allocates memory equal to the length of the entire string
    i = 1;
    int currLen = 0;
    // It loops through all the content in the parameters and concatenates parts of the string to create a complete string
    while(i != argc-1) {
        memcpy(combinedStr + currLen, parameters[i], strlen(parameters[i]));
        if(i != argc-3) {
            memcpy(combinedStr + currLen + strlen(parameters[i]), " ", 1);
        } else {
            memcpy(combinedStr + currLen + strlen(parameters[i]), ", ", 2);
            currLen++;
        }
        currLen += strlen(parameters[i]) + 1;
        i++;
    }

    combinedStr[len-1] = '\n'; // And new line char to end of string
    while(((byteswritten=write(txtFile, combinedStr, len))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
    //unlock the file
    lock.l_type=F_UNLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
    while(close(txtFile) == -1) ;



    // Get string for log
    // Define a buffer to hold the merged string
    char merged[100];
    // Merge the strings
    strcpy(merged, "Student has been succesfully added. Student -> ");
    strcat(merged, combinedStr); // Concatenate pointer to merged
    free(combinedStr);


    // If the string couldn't write to the file
    if(byteswritten < 0) {
        perror("Cannot write to the file");
        exit(-1);
    }
    saveLog(merged);
    exit(EXIT_SUCCESS);
}

/* Check if grade's size is 2 */
int isValidGrade(const char grade[]) {
    size_t len = strlen(grade);
    if (len != 2) // Grade must be exactly two characters long
        return 0;

    for (size_t i = 0; i < len; i++) {
        if (!isupper(grade[i])) // Grade must consist of capital letters
            return 0;
    }

    return 1;
}

/* It opens the file entered at input, searches for the entered student, and prints it to the terminal if it exists */
void searchStudent(int argc, char parameters[][MAX_SIZE]) {
    txtFile = open(fileName, O_RDONLY, 0555); // Opens the file for read-only
    if(txtFile == -1) {
        perror("The file cannot be opened");
        exit(-1);
    }
    char inputStudent[MAX_SIZE] = ""; // Array to hold input
    int i = 1;
    // Loops for all pieces in parameters and concatenate
    while(i != argc-2) {
        strcat(inputStudent, parameters[i]);
        strcat(inputStudent, " ");
        i++;
    }
    strcat(inputStudent, parameters[i]); // Add last piece later to prevent add space end of the string
    // Locks the file to prevent operations on the file at the same time as other operations
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));


    if(sigInt==1)
    {
        // Unlocks the file before exit to not block other proceses' operations
        printf("SIGINT caught by: %d\n", getpid());
        lock.l_type=F_UNLCK;
        while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
        while(close(txtFile) == -1) ;
        
        exit(-1);
    }

    int bytesread;
    
    char line[MAX_SIZE]; // Array to hold one line of the file
    char *nameSurname;
    unsigned char buffer[1];
    int index = 0;
    int equalFlag;
    memset(line, 0, sizeof(line));
    while(((bytesread=read(txtFile, buffer, 1)) > 0) && (errno != EINTR)) { // To make sure that the string is read correctly without interrupting
        // If the string couldn't read the file
        if(bytesread < 0) {
            perror("Cannot read from the file");
            //unlock the file
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            exit(-1);
        }
        if (buffer[0] != '\n') { // If the reading char is not end of line, adds to line array
            // Adds character to the current line buffer
            if(buffer[0] == ',') {
                nameSurname = line;
                if(strcasecmp(nameSurname, inputStudent) == 0) {
                    equalFlag = 1;
                }
            }
            line[index++] = buffer[0];
        } else {
            if(equalFlag == 1) {
                line[index] = '\0'; // Adds to determine it's end of the string
                printf("%s\n",line);

                // Define a buffer to hold the merged string
                char merged[100];
                // Merge the strings
                strcpy(merged, "searchStudent executed. Student has found. Student -> ");
                strcat(merged, line);
                strcat(merged, "\n");

                //unlock the file
                lock.l_type=F_UNLCK;
                while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
                while(close(txtFile) == -1) ;
                saveLog(merged);
                exit(EXIT_SUCCESS);
            }
            // If the line is not the correct student, empty the array to hold new line
            memset(line, 0, sizeof(line));
            index = 0;
        }
        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            
            exit(-1);
        }
    
    }
    if (equalFlag == 1) { // If the line contains input, the student has been found
        line[index] = '\0'; // Adds to determine it's end of the string

        char merged[100];
        // Merge the strings
        strcpy(merged, "searchStudent executed. Student has found. Student -> ");
        strcat(merged, line); // Concatenate line to merged
        strcat(merged, "\n");
        saveLog(merged); // Write operation to log

        printf("%s\n",line);
    } else {
        printf("Student doesn't exist\n");
        saveLog("searchStudent executed. Student couldn't find.\n");
    }
    //unlock the file
    lock.l_type=F_UNLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
    while(close(txtFile) == -1) ;
    exit(EXIT_SUCCESS);
}

/* Sorts the students i the file. It may sort by student name or grade, in ascending or descending order. Prints the students */
void sortAll(int argc, char parameters[][MAX_SIZE]) {
    if(strcmp(parameters[1], "name") != 0 && strcmp(parameters[1], "grade") != 0) { // If the given sort types are not correct, show the example usage and exit the process
        printf("Invalid sort type\n");
        printf("Types you can input\n1-name\n2-grade\n");
        printf("Example usage: sortAll name ascending example.txt\n");
        exit(EXIT_FAILURE);
    }
    if(strcmp(parameters[2], "-a") != 0 && strcmp(parameters[2], "ascending") != 0 
        && strcmp(parameters[2], "-d") != 0 && strcmp(parameters[2], "descending") != 0)  // If the given order types are not correct, show the example usage and exit the process
    {
        printf("Invalid order type\n");
        printf("Types you can input\n1-ascending(or -a)\n2-descending(or -d)\n");
        printf("Example usage: sortAll name ascending example.txt\n");
        
        exit(EXIT_FAILURE);
    }

    txtFile = open(fileName, O_RDONLY, 0555); // Opens the file as read-only
    if(txtFile == -1) {
        perror("The file cannot be opened");
        exit(-1);
    }

    // Locks the file to prevent operations on the file at the same time as other operations
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));

    if(sigInt==1)
    {
        printf("SIGINT caught by: %d\n", getpid());
        lock.l_type=F_UNLCK;
        while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
        while(close(txtFile) == -1) ;
        
        exit(-1);
    }

    int bytesread;
    
    char line[MAX_SIZE]; // Array to hold one line of the file
    unsigned char buffer[1];
    int index = 0;
    
    char **lines = malloc(MAX_SIZE * sizeof(char *));
    int lineCount = 0;
    memset(line, 0, sizeof(line));
    while(((bytesread=read(txtFile, buffer, 1)) > 0) && (errno != EINTR)) { // To make sure that the string is read correctly without interrupting
        // If the string couldn't read the file
        if(bytesread < 0) {
            perror("Cannot read from the file");
            //unlock the file
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            for (int i = 0; i < lineCount; i++) {
                free(lines[i]);
            }
            free(lines);
            exit(-1);
        }
        if (buffer[0] != '\n') { // If the reading char is not end of line adds to line array
            // Adds character to the current line buffer
            line[index++] = buffer[0];
        } else {
            line[index] = '\n'; // Adds to print correctly the line
            lines[lineCount] = strdup(line); // Make a copy of the line and sets to lines array
            lineCount++;
            memset(line, 0, sizeof(line));
            index = 0;
        }
        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            for (int i = 0; i < lineCount; i++) {
                free(lines[i]);
            }
            free(lines);
            exit(-1);
        }
    
    }

    //unlock the file
    lock.l_type=F_UNLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
    while(close(txtFile) == -1) ;
    if(strcmp(parameters[1], "name") == 0) { // If the sort type is "name", sorts the lines according to name
        qsort(lines, lineCount, sizeof(char *), compareLinesWithName);
    } else { // If the sort type is "grade", sorts the lines according to name
        qsort(lines, lineCount, sizeof(char *), compareLinesWithGrade);
    }
    if(strcmp(parameters[2], "-a") == 0 || strcmp(parameters[2], "ascending") == 0) { // If the order type is "ascending", prints the lines in ascending order
        // Print the sorted lines
        for (int i = 0; i < lineCount; i++) {
            printf("%s", lines[i]);
            free(lines[i]); // Free memory allocated by strdup
        }
    } else {
        // Print the sorted lines in descending order
        for (int i = lineCount-1; i > -1; i--) {
            printf("%s", lines[i]);
            free(lines[i]); // Free memory allocated by strdup
        }
    }
    // Free memory allocated for the array of lines
    free(lines);

    char merged[100];
    // Merge the strings
    strcpy(merged, "All students sorted and printed according to sort type -> ");
    strcat(merged, parameters[1]); // Concatenate parameter to merged
    strcat(merged, ", order type -> ");
    strcat(merged, parameters[2]); // Concatenate parameter to merged
    strcat(merged, "\n");
    saveLog(merged); // Write operation to log

    exit(EXIT_SUCCESS);
}

/* Function to compare two lines based on name */
int compareLinesWithName(const void *a, const void *b) {
    const char *line_a = *(const char **)a;
    const char *line_b = *(const char **)b;

    // Convert lines to lowercase before comparison
    char lowercase_a[strlen(line_a) + 1];
    char lowercase_b[strlen(line_b) + 1];
    strcpy(lowercase_a, line_a);
    strcpy(lowercase_b, line_b);
    for (int i = 0; lowercase_a[i]; i++)
        lowercase_a[i] = tolower(lowercase_a[i]);
    for (int i = 0; lowercase_b[i]; i++)
        lowercase_b[i] = tolower(lowercase_b[i]);

    return strcmp(lowercase_a, lowercase_b);
}

/* Function to compare two lines based on grade */
int compareLinesWithGrade(const void *a, const void *b) {
    // Cast parameters to pointers to pointers to char
    const char *line_a = *(const char **)a;
    const char *line_b = *(const char **)b;

    // Find the last space in each line
    const char *grade_a = strrchr(line_a, ' ');
    const char *grade_b = strrchr(line_b, ' ');

    return strcmp(grade_a, grade_b);
}

/* According to command it may prints all entries, first 5 entries or entries within a certain range */
void listEntries(int numOfEntries, int pageNumber) {
    txtFile = open(fileName, O_RDONLY, 0555); // Opens the file as read-only
    if(txtFile == -1) {
        perror("The file cannot be opened");
        
        exit(-1);
    }

    // Locks the file to prevent operations on the file at the same time as other operations
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));


    if(sigInt==1)
    {
        printf("SIGINT caught by: %d\n", getpid());
        lock.l_type=F_UNLCK;
        while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
        while(close(txtFile) == -1) ;
        
        exit(-1);
    }

    int bytesread;
    
    char line[MAX_SIZE];
    unsigned char buffer[1];
    int index = 0;
    int lineCount = 0;
    int pageCount = 1;
    memset(line, 0, sizeof(line));
    while(((bytesread=read(txtFile, buffer, 1)) > 0) && (errno != EINTR) && lineCount != numOfEntries) { // To make sure that the string is read correctly without interrupting
        // If the string couldn't read the file
        if(bytesread < 0) {
            perror("Cannot read from the file");
            //unlock the file
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            
            exit(-1);
        }
        if (buffer[0] != '\n') { // If the reading char is not end of line, adds to line array
            // Adds the character to the current line
            line[index++] = buffer[0];
        } else {
            line[index] = '\n';
            if(pageCount == pageNumber) {
                printf("%s",line);
            }
            memset(line, 0, sizeof(line)); // Empty the array to hold new line
            index = 0;
            lineCount++;
            if(lineCount == numOfEntries && pageCount != pageNumber) {
                pageCount++;
                lineCount = 0;
            }
        }
        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            lock.l_type=F_UNLCK;
            while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(txtFile) == -1) ;
            exit(-1);
        }
    }
    //unlock the file
    lock.l_type=F_UNLCK;
    while((fcntl(txtFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
    while(close(txtFile) == -1) ;

    char merged[100];
    if(numOfEntries == -1) {
        strcpy(merged, "All students listed\n");
    } else if(numOfEntries == 5 && pageNumber == 1) {
        strcpy(merged, "First 5 entries listed\n");
    } else {
        // Make merged string for the listSome command
        char str[20];
        sprintf(str, "%d", numOfEntries);
        strcpy(merged, "Listed ");
        strcat(merged, str);
        strcat(merged, " entries in page ");
        sprintf(str, "%d", pageNumber);
        strcat(merged, str);
        strcat(merged, "\n");
    }
    saveLog(merged); // Write operation to log

    exit(EXIT_SUCCESS);
}

/* Checks if the given chars are digit or not */
int checkDigit(char token[MAX_SIZE]) {
    // Check if token contains only digits
    for (int i = 0; token[i] != '\0'; i++) {
        if (!isdigit(token[i])) {
            return 0; // Not all characters are digits
        }
    }
    return 1;
}

/* Saves the current operation to log file */
void saveLog (char *errorLog) {
    pid_t pid = fork(); // Create new process
    if(pid == -1) {
        perror("Fork failed");
        exit(1);
    } else if(pid == 0) { // If process is child. Call the specific function according to p
        int logFile = open("log", O_WRONLY | O_CREAT | O_APPEND, 0777); // Creates the file or discard content of the file if already exist.
        if(logFile == -1) {
            perror("The file cannot be opened");
            exit(-1);
        }

        // Locks the file to prevent operations on the file at the same time as other operations
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        while((fcntl(logFile, F_SETLKW, &lock) == -1) && (errno == EINTR));

        if(sigInt==1)
        {
            printf("SIGINT caught by: %d\n", getpid());
            lock.l_type=F_UNLCK;
            while((fcntl(logFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
            while(close(logFile) == -1) ;
            exit(-1);
        }
        int byteswritten;
        char *timestamp = get_timestamp(); // Get local time
        timestamp[strlen(timestamp) - 1] = '\0'; // Remove \n
        strcat(timestamp, " - ");
        size_t length = strlen(timestamp);
        while(((byteswritten=write(logFile, timestamp, length))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
        while(((byteswritten=write(logFile, errorLog, strlen(errorLog)))==-1) && (errno==EINTR)); // To make sure that the string is written correctly without interrupting
        //unlock the file
        lock.l_type=F_UNLCK;
        while((fcntl(logFile, F_SETLKW, &lock) == -1) && (errno == EINTR));
        while(close(logFile) == -1) ;
        // If the string couldn't write to the file
        if(byteswritten < 0) {
            perror("Cannot write to the file");
            exit(-1);
        }
        exit(EXIT_SUCCESS);
    } else {
        waitpid(pid,NULL,0);
    }
}

/* Function to get local time */
char* get_timestamp() {
    time_t now = time (NULL);
    return asctime (localtime (&now));
}
