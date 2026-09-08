#include <stdio.h>
#include "user.h"

void showMenu()
{
    printf("\n");
    printf("USER MANAGEMENT PROGRAM\n");
    printf("-----------------------------------\n");
    printf("1. Register\n");
    printf("2. Sign in\n");
    printf("3. Search\n");
    printf("4. Sort\n");
    printf("Your choice (1-4, other to quit): ");
}

int main()
{
    User *head = NULL;
    int choice;

    while (1)
    {
        showMenu();

        scanf("%d", &choice);

        switch (choice)
        {
            case 1:
               registerUser(&head);
                break;

            case 2:
                printf("Sign in selected.\n");
                break;

            case 3:
                printf("Search selected.\n");
                break;

            case 4:
                printf("Sort selected.\n");
                break;

            default:
                printf("Program terminated.\n");
                return 0;
        }
    }

    return 0;
}