#include <iostream>
#include <vector>

using namespace std;

// Function to check if a given position is valid or not
bool isValid(int i, int j, int n) {
    return (i >= 0 && i < n && j >= 0 && j < n);
}

// Recursive function to find the longest path in the maze
void findLongestPath(vector<vector<int>>& maze, vector<vector<int>>& solution, int i, int j, int& maxLength) {
    int n = maze.size(); // Size of the maze

    // Check if the current position is out of the matrix or not valid
    if (!isValid(i, j, n) || maze[i][j] == 0 || solution[i][j] == 1) {
        return;
    }

    // Mark the current position as part of the solution path
    solution[i][j] = 1;

    // Check if the current position is the destination
    if (i == n - 1 && j == n - 1) {
        // Print the solution matrix
        cout << "Solution:" << endl;
        for (int x = 0; x < n; x++) {
            for (int y = 0; y < n; y++) {
                cout << solution[x][y] << " ";
            }
            cout << endl;
        }
        cout << endl;
        maxLength = max(maxLength, solution[n - 1][n - 1]);
    }

    // Recursive calls in four directions (up, down, left, right)
    for (int k = 1; k <= maze[i][j]; k++) {
        // Move right
        findLongestPath(maze, solution, i, j + k, maxLength);
        // Move left
        findLongestPath(maze, solution, i, j - k, maxLength);
        // Move down
        findLongestPath(maze, solution, i + k, j, maxLength);
        // Move up
        findLongestPath(maze, solution, i - k, j, maxLength);
    }

    // Unmark the current position as it does not lead to a valid path
    solution[i][j] = 0;
}

int main() {
    int n;
    cout << "Enter the size of the maze (N): ";
    cin >> n;

    vector<vector<int>> maze(n, vector<int>(n));
    cout << "Enter the maze values:" << endl;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            cin >> maze[i][j];
        }
    }

    // Print the input maze
    cout << "Input Maze:" << endl;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            cout << maze[i][j] << " ";
        }
        cout << endl;
    }
    cout << endl;

    vector<vector<int>> solution(n, vector<int>(n, 0)); // Initialize solution matrix

    int maxLength = 0; // To store the length of the longest path

    findLongestPath(maze, solution, 0, 0, maxLength);

    if (maxLength > 0) {
        cout << "Longest path length: " << maxLength << endl;
    } else {
        cout << "No path exists." << endl;
    }

    return 0;
}
