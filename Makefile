#make file - use 'make clean' command to compile
 
clean:  #target name
		$(RM) oss
		$(RM) user_proc
		gcc user_proc.c sharedFunctions.c -o user_proc
		gcc oss.c  sharedFunctions.c -o oss