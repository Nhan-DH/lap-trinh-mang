#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"

static int isLoggedIn = 0;

// tạo User mới
User *createUser(const char *username,
                 const char *password,
                 int status,
                 float score)
{
    User *newUser = malloc(sizeof(User));

    if (newUser == NULL)
    {
        printf("Memory allocation failed!\n");
        return NULL;
    }

    strcpy(newUser->username, username);
    strcpy(newUser->password, password);

    newUser->status = status;
    newUser->score = score;

    newUser->next = NULL;

    return newUser;
}

// thêm user vào cuối danh sách
void addUser(User **head, User *newUser)
{
    if (*head == NULL)
    {
        *head = newUser;
        return;
    }

    User *current = *head;

    while (current->next != NULL)
    {
        current = current->next;
    }
    current->next = newUser;
}

// tìm user trong linked list theo username
static User *findUser(User *head, const char *username)
{
    User *current = head;

    while (current != NULL)
    {
        if (strcmp(current->username, username) == 0)
        {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

// đọc user từ file user.txt
User *loadUsers(void)
{
    FILE *file = fopen("user.txt", "r");

    if (file == NULL)
    {
        /*
         * Nếu chưa có file thì tạo file rỗng
         */
        file = fopen("user.txt", "w");
        if (file == NULL)
        {
            perror("Cannot create user.txt");
            return NULL;
        }

        fclose(file);

        return NULL;
    }

    User *head = NULL;

    char line[200];

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char username[USERNAME_SIZE];
        char password[PASSWORD_SIZE];

        int status;
        float score;
        /*
         * Đọc dữ liệu theo dạng:
         * username:password:status:score
         */
        if (sscanf(line,
                 "%49[^:]:%49[^:]:%d:%f",
                   username,
                   password,
                   &status,
                   &score) != 4)
        {
            printf("Warning: invalid data in user.txt\n");
            continue;
        }

        User *newUser = createUser(
            username,
            password,
            status,
            score
        );

        if (newUser == NULL)
        {
            fclose(file);
            return head;
        }

        addUser(&head, newUser);
    }

    fclose(file);

    return head;
}



// Lưu danh sách User vào user.txt
int saveUsers(User *head)
{
    FILE *file = fopen("user.txt", "w");

    if (file == NULL)
    {
        perror("Cannot open user.txt");
        return 0;
    }

    User *current = head;

    while (current != NULL)
    {
        fprintf(file,
                "%s:%s:%d:%.2f\n",
                current->username,
                current->password,
                current->status,
                current->score);

        current = current->next;
    }

    fclose(file);

    return 1;
}


// Chức năng Register
void registerUser(User **head)
{
    char username[USERNAME_SIZE];
    char password[PASSWORD_SIZE];

    float score;

    printf("\n");
    printf("===== REGISTER =====\n");
   
    // 1. Nhập username
    printf("Enter username: ");
    scanf("%49s", username);
    // 2. Kiểm tra username đã tồn tại chưa
    User *existingUser = findUser(*head, username);

    if (existingUser != NULL)
    {
        printf("Error: Username already exists!\n");
        return;
    }
    // 3. Nhập password
    printf("Enter password: ");
    scanf("%49s", password);
    // 4. Nhập score
    printf("Enter score: ");
    scanf("%f", &score);
   // 5. Tạo User mới với status = 0 (active)
    User *newUser = createUser(
        username,
        password,
        0,
        score
    );
    // Kiểm tra xem User mới có được tạo thành công không
    if (newUser == NULL)
    {
        printf("Error: Cannot create user!\n");
        return;
    }
   // 6. Thêm User mới vào danh sách
    addUser(head, newUser);
    // 7. Lưu danh sách User vào file user.txt
    if (saveUsers(*head))
    {
        printf("Register successfully!\n");
    }
    else
    {
        printf("Error: Cannot save user to user.txt!\n");
    }
}

// Chức năng Sign In
void signIn(User **head)
{
    char username[USERNAME_SIZE];
    char password[PASSWORD_SIZE];

    int wrongCounts = 0;

    printf("\n");
    printf("===== SIGN IN =====\n");

    // 1. Nhập username
    printf("Enter username: ");
    scanf("%49s", username);

    // 2. Tìm username trong user.txt
    User *user = findUser(*head, username);

    // 3. Nếu username không tồn tại
    if (user == NULL)
    {
        printf("Error: Username does not exist!\n");
        return;
    }

    // 4. Kiểm tra tài khoản có bị khóa hay không
    if (user->status == 1)
    {
        printf("Error: Account is blocked!\n");
        return;
    }
    // 5. Cho phép người dùng nhập password
    // Có thể nhập lại nếu password sai
    while (wrongCounts <= 3)
    {
        // 6. Nhập password
        printf("Enter password: ");
        scanf("%49s", password);

        // 7. Kiểm tra password có đúng hay không
        if (strcmp(user->password, password) == 0)
        {
            isLoggedIn = 1;
            printf("Login successful!\n");
            return;
        }

        // 8. Password sai → tăng số lần nhập sai
        wrongCounts++;

        printf("Error: Incorrect password!\n");
        // 9. Nếu sai quá 3 lần → khóa tài khoản
        if (wrongCounts > 3)
        {
            if (user != NULL)
            {
                user->status = 1;
            }
            // 10. Lưu trạng thái tài khoản vào file user.txt
            if (saveUsers(*head))
            {
                printf("Account has been blocked!\n");
            }
            else
            {
                printf("Error: Cannot update user.txt!\n");
            }
            return;
        }
    }
}

// Chức năng Search
void searchUser(User *head)
{
    char username[USERNAME_SIZE];

    printf("\n");
    printf("===== SEARCH =====\n");

    if (!isLoggedIn)
    {
        printf("Error: You must sign in first!\n");
        return;
    }

    printf("Enter username: ");
    scanf("%49s", username);

    User *user = findUser(head, username);

    if (user == NULL)
    {
        printf("Error: Username does not exist!\n");
        return;
    }

    printf("Username: %s\n", user->username);
    printf("Status: %s\n", user->status == 0 ? "active" : "blocked");
}

