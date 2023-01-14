//
// Created by jonespinosa89 on 11/8/22.
//
#include <stdio.h>  /* for perror() */
#include <stdlib.h> /* for exit() */
#include <unistd.h>

void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}
