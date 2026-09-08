#ifndef USER_H
#define USER_H

#define USERNAME_SIZE 50 //toi da 49 ki tu
#define PASSWORD_SIZE 50

typedef struct User
{
    char username[USERNAME_SIZE];
    char password[PASSWORD_SIZE];
    int status;
    float score;

    struct User *next;
} User;

// ====Các hàm xử lý User===
// Tạo User mới
User *createUser(const char *username,
                 const char *password,
                 int status,
                 float score);

//thêm user vào danh sách
void addUser(User **head, User *newUser);

// đọc user từ file user.txt
User *loadUsers(void);

// tìm kiếm user theo username
User *findUser(User *head, const char *username);

// Lưu danh sách User vào user.txt
int saveUsers(User *head);


// ====Các hàm chức năng của chương trình===
// Chức năng Register
void registerUser(User **head);

// Chức năng Sign In
void signIn(User **head);

#endif