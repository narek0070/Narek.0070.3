#include <stdio.h>
int main() {
    int n = 0;
    int sum = 0;

    printf ("Enter your Digit");
    scanf("%d", &n);

    for(int i = 1; i <= n; i++) {
        sum += i;
        printf("%d\n", i);
      }

        printf("Sum: %d\n", sum);

        return 0;
    }
