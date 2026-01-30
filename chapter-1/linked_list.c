/**
 * Doubly Linked List in C
 * 
 * This program implements a doubly linked list of heap-allocated strings.
 * It demonstrates:
 * - Pointer manipulation
 * - Manual memory management (malloc/free)
 * - String handling in C
 * - Doubly linked list operations: insert, find, delete
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Node structure for doubly linked list
 * Contains a string and pointers to next and previous nodes
 */
typedef struct Node {
    char *data;           // Heap-allocated string
    struct Node *next;    // Pointer to next node
    struct Node *prev;    // Pointer to previous node
} Node;

/**
 * Create a new node with the given string
 * Allocates memory for both the node and the string
 */
Node* createNode(const char *str) {
    Node *newNode = (Node*)malloc(sizeof(Node));
    if (newNode == NULL) {
        fprintf(stderr, "Memory allocation failed for node\n");
        return NULL;
    }
    
    // Allocate memory for the string and copy it
    newNode->data = (char*)malloc(strlen(str) + 1);
    if (newNode->data == NULL) {
        fprintf(stderr, "Memory allocation failed for string\n");
        free(newNode);
        return NULL;
    }
    
    strcpy(newNode->data, str);
    newNode->next = NULL;
    newNode->prev = NULL;
    
    return newNode;
}

/**
 * Insert a new node at the beginning of the list
 * Returns the new head of the list
 */
Node* insertAtBeginning(Node *head, const char *str) {
    Node *newNode = createNode(str);
    if (newNode == NULL) return head;
    
    if (head == NULL) {
        return newNode;
    }
    
    newNode->next = head;
    head->prev = newNode;
    return newNode;
}

/**
 * Insert a new node at the end of the list
 * Returns the head (unchanged)
 */
Node* insertAtEnd(Node *head, const char *str) {
    Node *newNode = createNode(str);
    if (newNode == NULL) return head;
    
    if (head == NULL) {
        return newNode;
    }
    
    // Traverse to the end
    Node *current = head;
    while (current->next != NULL) {
        current = current->next;
    }
    
    current->next = newNode;
    newNode->prev = current;
    return head;
}

/**
 * Find a node with the given string
 * Returns pointer to the node if found, NULL otherwise
 */
Node* findNode(Node *head, const char *str) {
    Node *current = head;
    while (current != NULL) {
        if (strcmp(current->data, str) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * Delete a node with the given string
 * Returns the (possibly new) head of the list
 */
Node* deleteNode(Node *head, const char *str) {
    Node *nodeToDelete = findNode(head, str);
    
    if (nodeToDelete == NULL) {
        printf("Node with value '%s' not found\n", str);
        return head;
    }
    
    // Update pointers
    if (nodeToDelete->prev != NULL) {
        nodeToDelete->prev->next = nodeToDelete->next;
    }
    if (nodeToDelete->next != NULL) {
        nodeToDelete->next->prev = nodeToDelete->prev;
    }
    
    // If we're deleting the head, return the new head
    Node *newHead = head;
    if (nodeToDelete == head) {
        newHead = head->next;
    }
    
    // Free the memory
    free(nodeToDelete->data);
    free(nodeToDelete);
    
    return newHead;
}

/**
 * Display the list from head to tail
 */
void displayForward(Node *head) {
    printf("Forward: ");
    if (head == NULL) {
        printf("[empty list]\n");
        return;
    }
    
    Node *current = head;
    while (current != NULL) {
        printf("[%s]", current->data);
        if (current->next != NULL) printf(" <-> ");
        current = current->next;
    }
    printf("\n");
}

/**
 * Display the list from tail to head
 */
void displayBackward(Node *head) {
    printf("Backward: ");
    if (head == NULL) {
        printf("[empty list]\n");
        return;
    }
    
    // Find the tail
    Node *current = head;
    while (current->next != NULL) {
        current = current->next;
    }
    
    // Display backward
    while (current != NULL) {
        printf("[%s]", current->data);
        if (current->prev != NULL) printf(" <-> ");
        current = current->prev;
    }
    printf("\n");
}

/**
 * Free all nodes in the list
 */
void freeList(Node *head) {
    Node *current = head;
    while (current != NULL) {
        Node *temp = current;
        current = current->next;
        free(temp->data);
        free(temp);
    }
}

/**
 * Test the doubly linked list implementation
 */
int main(void) {
    printf("=== Doubly Linked List of Strings ===\n\n");
    
    Node *list = NULL;
    
    // Test 1: Insert at end
    printf("Test 1: Inserting at end\n");
    list = insertAtEnd(list, "Alice");
    list = insertAtEnd(list, "Bob");
    list = insertAtEnd(list, "Charlie");
    displayForward(list);
    displayBackward(list);
    printf("\n");
    
    // Test 2: Insert at beginning
    printf("Test 2: Inserting 'Zoe' at beginning\n");
    list = insertAtBeginning(list, "Zoe");
    displayForward(list);
    displayBackward(list);
    printf("\n");
    
    // Test 3: Find a node
    printf("Test 3: Finding nodes\n");
    Node *found = findNode(list, "Bob");
    if (found != NULL) {
        printf("Found: '%s'\n", found->data);
    }
    found = findNode(list, "David");
    if (found == NULL) {
        printf("'David' not found in list\n");
    }
    printf("\n");
    
    // Test 4: Delete a node
    printf("Test 4: Deleting 'Bob'\n");
    list = deleteNode(list, "Bob");
    displayForward(list);
    displayBackward(list);
    printf("\n");
    
    // Test 5: Delete head
    printf("Test 5: Deleting head ('Zoe')\n");
    list = deleteNode(list, "Zoe");
    displayForward(list);
    displayBackward(list);
    printf("\n");
    
    // Test 6: Delete tail
    printf("Test 6: Deleting tail ('Charlie')\n");
    list = deleteNode(list, "Charlie");
    displayForward(list);
    displayBackward(list);
    printf("\n");
    
    // Test 7: Delete the last remaining node
    printf("Test 7: Deleting 'Alice' (last node)\n");
    list = deleteNode(list, "Alice");
    displayForward(list);
    printf("\n");
    
    // Test 8: Try to delete from empty list
    printf("Test 8: Deleting from empty list\n");
    list = deleteNode(list, "NonExistent");
    printf("\n");
    
    // Clean up
    freeList(list);
    printf("All memory freed successfully!\n");
    
    return 0;
}