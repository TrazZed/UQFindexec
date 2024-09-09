# UQFindexec

uqfindexec program will execute a specified command, or pipeline of commands, for each file found in a specified directory. If the string {} appears in the command(s), then it will be replaced by the name of the file being processed. 

./uqfindexec [--directory _dir_] [--statistics] [--parallel] [--all] [--descend] [_cmd_]

For example, 

./uqfindexec --directory /etc "wc -l {}"

will run the command

wc -l /etc/_filename_

for every file found in /etc, where _filename_ is replaced by teh name of each file in turn.
