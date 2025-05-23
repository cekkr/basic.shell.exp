/*
 * bsh - The Extensible Shell
 * Version: 0.8 (Dot Notation Property Access)
 * Copyright: Riccardo Cecchini <rcecchini.ds@gmail.com>
 *
 * This is an extensible shell designed to be lightweight yet powerful,
 * allowing much of its functionality—including its operational syntax and
 * data handling—to be defined and extended through BSH scripts themselves,
 * rather than being hardcoded in C.
 *
 * --- Core Extensibility Mechanisms ---
 *
 * bsh achieves its flexibility and extensibility through several key mechanisms:
 *
 * 1.  **Script-Defined Operators (`defoperator`):**
 * Unlike traditional shells with fixed operator sets, bsh allows BSH scripts
 * to define most operator symbols (e.g., "+", "-", "*", "++", "--", ".", "==")
 * at runtime using the `defoperator` built-in command. The C core's tokenizer
 * learns these operator symbols from script definitions.
 *
 * 2.  **Unified Operator Dispatch (`__dynamic_op_handler`):**
 * When the C core tokenizes an expression involving a script-defined operator
 * (be it binary infix like `$a + $b`, or unary prefix/postfix like `++$var`
 * or `$var++`), it does not interpret the operation directly. Instead, it
 * invokes a single, designated BSH function, conventionally named
 * `__dynamic_op_handler`. This BSH function receives the operands (or variable
 * name for unary ops), the operator symbol, and context (e.g., "prefix",
 * "postfix"). The `__dynamic_op_handler` (defined in a core BSH script)
 * is then responsible for determining the precise action, often involving
 * type checking (via `type.bsh`) and calling appropriate BSH action functions
 * (e.g., from `number.bsh`). This makes arithmetic, comparisons, and other
 * operations fully extensible and customizable at the script level.
 *
 * 3.  **Structured Data Handling (`object:` prefix & `echo` stringification):**
 * -   **Automatic Parsing:** If a command's standard output (when captured, e.g.,
 * during variable assignment like `$mydata = $(command_outputs_object)`)
 * begins with the prefix "object:", `bsh.c` automatically parses the
 * subsequent string as a BSH-specific object literal (e.g.,
 * `["key1":"value1", "key2":["subkey":"subval"]]`). This parsed data is
 * then "flattened" into a set of standard BSH variables, accessible via
 * dot notation (e.g., `$mydata.key1`, `$mydata.key2.subkey`).
 * A metadata variable (e.g., `$mydata_BSH_STRUCT_TYPE = "BSH_OBJECT_ROOT"`)
 * is also created to mark the base variable as a BSH object container.
 * -   **Automatic Stringification by `echo`:** Conversely, if the `echo` command
 * is given a variable that represents one of these BSH objects (identified
 * by its `_BSH_STRUCT_TYPE` metadata), `echo` will automatically "stringify"
 * the object, converting the flattened BSH variables back into the
 * `object:["key":"value", ...]` string format for output.
 * -   A BSH library (e.g., `object.bsh` or `extensions.bsh`) can provide helper functions to
 * more easily navigate and manipulate these flattened object structures within scripts.
 *
 * 4.  **Variable Property Access (Dot Notation):**
 * To access elements within BSH "objects" (which are internally stored as
 * flattened sets of variables using an underscore-separated naming convention,
 * e.g., `basevar_property1_property2`), bsh primarily uses a dot notation
 * implemented in C's variable expansion logic:
 * -   `$variable.propertyName` (e.g., `$myobj.user` accesses `myobj_user`)
 * -   `$variable.$variableContainingPropertyName` (e.g., if `$keyvar` holds "name",
 * then `$myobj.user.$keyvar` accesses `myobj_user_name`)
 * This syntax provides a clean and intuitive way to navigate the structured
 * data. The C core (`expand_variables_in_string_advanced`) parses this dot
 * notation and resolves it to the corresponding flattened BSH variable name for lookup.
 * While dot notation is the C-implemented default, BSH scripting extensions
 * can provide functions to simulate other access styles (like `[]`) by
 * programmatically constructing and accessing these underlying flattened variable names.
 *
 * 5.  **Dynamic PATH and Module Resolution (`import`):**
 * Commands and BSH modules (framework scripts) are resolved dynamically by
 * searching paths defined in `PATH` (for executables) and `BSH_MODULE_PATH`
 * (for BSH scripts). The `import module.name` command loads BSH scripts,
 * converting dots in the module name to directory slashes to locate the file
 * (e.g., `import framework.number` looks for `framework/number.bsh` within
 * `BSH_MODULE_PATH` directories). This allows for modular script organization.
 *
 * 6.  **On-the-Fly C Compilation (`c_compiler.bsh` framework):**
 * Through a BSH framework script like `c_compiler.bsh` (which provides a
 * BSH function like `def_c_lib`), users can define, compile (using an
 * available system C compiler like gcc or clang), and load C code as shared
 * libraries at runtime.
 *
 * 7.  **Dynamic Library Loading & Calling (`loadlib`, `calllib`):**
 * Once C code is compiled, the resulting shared library can be loaded into bsh
 * using the `loadlib <path> <alias>` built-in. Functions within this loaded
 * library can then be invoked from BSH scripts using
 * `calllib <alias> <func_name> [args...]`. This is crucial for
 * extending the shell with high-performance operations (e.g., the `bshmath`
 * library for `number.bsh`) or accessing system APIs.
 *
 * 8.  **User-Defined Functions (`function` keyword):**
 * The `function` (or `defunc`) keyword allows users to define custom shell
 * functions entirely within BSH script. These functions support parameters
 * and lexical scoping (variables defined within a function are local by default),
 * enabling complex logic and code reuse directly in BSH. Parentheses `()`
 * are used in function definitions to declare parameters, e.g.,
 * `function my_func (param1 param2) { ... }`. Function calls follow
 * typical shell syntax: `my_func arg1 arg2`.
 *
 * 9.  **Keyword Aliasing (`defkeyword`):**
 * The `defkeyword` built-in command allows users to create aliases for existing
 * keywords or command words (e.g., `defkeyword defunc function`). This helps
 * in personalizing the shell's syntax or maintaining compatibility.
 *
 * --- Parentheses `()` Usage ---
 *
 * In bsh, parentheses `()` are currently primarily used for:
 * -   **Function Definitions:** To enclose the parameter list during function
 * declaration, e.g., `function my_func (param1 param2) { ... }`.
 * -   **Expression Grouping (Conceptual):** While the C core does not perform
 * arithmetic precedence parsing using parentheses, they are tokenized as
 * `TOKEN_LPAREN` and `TOKEN_RPAREN`. This allows them to be passed as part of
 * expressions to the BSH `__dynamic_op_handler`, which could then implement
 * script-level evaluation logic that respects parentheses if desired.
 * They are not currently used by the C core for command grouping in subshells.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <dlfcn.h> // For dynamic library loading
#include <errno.h>
#include <limits.h> // For PATH_MAX (usually) or alternative for buffer sizes
#include <libgen.h> // For dirname, basename (often part of string.h or stdlib.h but good to be explicit)

// --- Constants and Definitions ---
#define MAX_LINE_LENGTH 2048
#define MAX_ARGS 128
#define MAX_VAR_NAME_LEN 256
// #define MAX_VAR_VALUE_LEN 2048 // No longer used for Variable->value storage
#define INPUT_BUFFER_SIZE 4096 // For reading lines, command output before strdup
#define MAX_FULL_PATH_LEN 1024 // For constructing full paths
#ifndef PATH_MAX // Define PATH_MAX if not defined (common on some systems)
    #ifdef _XOPEN_PATH_MAX
        #define PATH_MAX _XOPEN_PATH_MAX
    #else
        #define PATH_MAX 4096 // Fallback
    #endif
#endif
#define TOKEN_STORAGE_SIZE (MAX_LINE_LENGTH * 2)
#define MAX_NESTING_DEPTH 32 // For if/while/function call block nesting
#define MAX_FUNC_LINES 100   // Max lines in a user-defined function body
#define MAX_FUNC_PARAMS 10   // Max parameters for a user-defined function
#define MAX_OPERATOR_LEN 8   // Max length of an operator string (e.g., "==")
#define DEFAULT_STARTUP_SCRIPT ".bshrc" // Shell startup script
#define MAX_KEYWORD_LEN 32   // Max length for a keyword or its alias
#define MAX_SCOPE_DEPTH 64   // Max depth for lexical scopes (function calls)
#define DEFAULT_MODULE_PATH "./framework:~/.bsh_framework:/usr/local/share/bsh/framework" // Example default module path

#define JSON_STDOUT_PREFIX "json:"
#define OBJECT_STDOUT_PREFIX "object:" // You can decide if "object:" is treated same as "json:"

// --- Tokenizer Types ---
typedef enum {
    TOKEN_EMPTY, TOKEN_WORD, TOKEN_STRING, TOKEN_VARIABLE, TOKEN_OPERATOR,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE, TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_ASSIGN, TOKEN_SEMICOLON, TOKEN_PIPE, TOKEN_AMPERSAND, TOKEN_COMMENT,
    TOKEN_EOF, TOKEN_ERROR, TOKEN_NUMBER,
    TOKEN_QMARK, TOKEN_COLON 
} TokenType;

typedef struct {
    TokenType type;
    const char *text; // Points into the token_storage buffer
    int len;
} Token;

// --- Operator Definition (Dynamic List) ---
typedef struct OperatorDefinition {
    char op_str[MAX_OPERATOR_LEN + 1];
    TokenType op_type;
    struct OperatorDefinition *next;
} OperatorDefinition;
OperatorDefinition *operator_list_head = NULL;

// --- Keyword Aliasing (Dynamic List) ---
typedef struct KeywordAlias {
    char original[MAX_KEYWORD_LEN + 1];
    char alias[MAX_KEYWORD_LEN + 1];
    struct KeywordAlias *next;
} KeywordAlias;
KeywordAlias *keyword_alias_head = NULL;

// --- PATH Directories (Dynamic List) ---
typedef struct PathDirNode {
    char *path; // Dynamically allocated string for the directory path
    struct PathDirNode *next;
} PathDirNode;
PathDirNode *path_list_head = NULL; // For executables
PathDirNode *module_path_list_head = NULL; // For BSH modules/framework scripts

// --- Variable Scoping and Management ---
typedef struct Variable {
    char name[MAX_VAR_NAME_LEN];
    char *value; // Dynamically allocated string for the variable's value
    bool is_array_element;
    int scope_id; // Identifier for the scope this variable belongs to
    struct Variable *next; // Next variable in the global list
} Variable;
Variable *variable_list_head = NULL; // Global list of all variables

typedef struct ScopeFrame {
    int scope_id;
    // Other scope-specific info could go here if needed
} ScopeFrame;
ScopeFrame scope_stack[MAX_SCOPE_DEPTH];
int scope_stack_top = -1; // -1 means global scope (scope_id 0) is not yet formally pushed
int next_scope_id = 1;    // Counter for unique scope IDs (0 is global)
#define GLOBAL_SCOPE_ID 0

// --- User-Defined Functions ---
typedef struct UserFunction {
    char name[MAX_VAR_NAME_LEN];
    char params[MAX_FUNC_PARAMS][MAX_VAR_NAME_LEN];
    int param_count;
    char* body[MAX_FUNC_LINES]; // Array of strings (lines of the function body)
    int line_count;
    struct UserFunction *next;
} UserFunction;
UserFunction *function_list = NULL;
bool is_defining_function = false;
UserFunction *current_function_definition = NULL;

// --- Execution State and Block Management (for if/while etc.) ---
typedef enum {
    STATE_NORMAL, STATE_BLOCK_EXECUTE, STATE_BLOCK_SKIP,
    STATE_DEFINE_FUNC_BODY, STATE_IMPORT_PARSING
} ExecutionState;
ExecutionState current_exec_state = STATE_NORMAL;

typedef enum {
    BLOCK_TYPE_IF, BLOCK_TYPE_ELSE, BLOCK_TYPE_WHILE, BLOCK_TYPE_FUNCTION_DEF
} BlockType;

typedef struct BlockFrame { // For if/while/else block control flow
    BlockType type;
    long loop_start_fpos;        // File position for `while` loop start (for fseek)
    int loop_start_line_no;      // Line number for `while` loop start
    bool condition_true;         // Was the entry condition for if/while true?
    ExecutionState prev_exec_state; // Execution state before entering this block
} BlockFrame;
BlockFrame block_stack[MAX_NESTING_DEPTH]; // Stack for if/while blocks
int block_stack_top_bf = -1; // Top of the block_stack (bf for Block Frame)

// --- Dynamic Library Handles ---
typedef struct DynamicLib {
    char alias[MAX_VAR_NAME_LEN];
    void *handle;
    struct DynamicLib *next;
} DynamicLib;
DynamicLib *loaded_libs = NULL;


// --- Function Prototypes ---

// Core
void initialize_shell();
void process_line(char *line, FILE *input_source, int current_line_no, ExecutionState exec_mode);
void execute_script(const char *filename, bool is_import, bool is_startup_script);
void cleanup_shell();

// Tokenizer & Keyword Aliasing
void initialize_operators_dynamic();
void add_operator_dynamic(const char* op_str, TokenType type);
int match_operator_dynamic(const char *input, const char **op_text, TokenType *matched_type);
void add_keyword_alias(const char* original, const char* alias_name);
const char* resolve_keyword_alias(const char* alias_name);
void free_keyword_alias_list();
int advanced_tokenize_line(const char *line, Token *tokens, int max_tokens, char *token_storage, size_t storage_size);
bool evaluate_expression_tokens(Token* tokens, int start_idx, int end_idx, char* result_buffer, size_t buffer_size);

// Path Management
void add_path_to_list(PathDirNode **list_head, const char* dir_path);
void free_path_dir_list(PathDirNode **list_head);
void initialize_module_path();

// Variable & Scope Management
int enter_scope();
void leave_scope(int scope_id_to_leave);
void cleanup_variables_for_scope(int scope_id);
char* get_variable_scoped(const char *name_raw);
void set_variable_scoped(const char *name_raw, const char *value_to_set, bool is_array_elem);
void expand_variables_in_string_advanced(const char *input_str, char *expanded_str, size_t expanded_str_size);
char* get_array_element_scoped(const char* array_base_name, const char* index_str_raw);
void set_array_element_scoped(const char* array_base_name, const char* index_str_raw, const char* value);

// Command Execution
bool find_command_in_path_dynamic(const char *command, char *full_path);
bool find_module_in_path(const char* module_name, char* full_path);
int execute_external_command(char *command_path, char **args, int arg_count, char *output_buffer, size_t output_buffer_size);
void execute_user_function(UserFunction* func, Token* call_arg_tokens, int call_arg_token_count, FILE* input_source_for_context);

// Built-in Commands & Operation Handlers
void handle_defoperator_statement(Token *tokens, int num_tokens) 
void handle_defkeyword_statement(Token *tokens, int num_tokens);
void handle_assignment_advanced(Token *tokens, int num_tokens);
void handle_echo_advanced(Token *tokens, int num_tokens);
bool evaluate_condition_advanced(Token* operand1_token, Token* operator_token, Token* operand2_token);
void handle_if_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no);
void handle_else_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no);
void handle_while_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no);
void handle_defunc_statement_advanced(Token *tokens, int num_tokens);
void handle_inc_dec_statement_advanced(Token *tokens, int num_tokens, bool increment); // For 'inc'/'dec' keywords
void handle_loadlib_statement(Token *tokens, int num_tokens);
void handle_calllib_statement(Token *tokens, int num_tokens);
void handle_import_statement(Token *tokens, int num_tokens);
void handle_update_cwd_statement(Token *tokens, int num_tokens);
void handle_unary_op_statement(Token* var_token, Token* op_token, bool is_prefix); // For ++$var, $var++
void handle_exit_statement(Token *tokens, int num_tokens);
void handle_eval_statement(Token *tokens, int num_tokens);

// Block Management (for if/while etc.)
void push_block_bf(BlockType type, bool condition_true, long loop_start_fpos, int loop_start_line_no);
BlockFrame* pop_block_bf();
BlockFrame* peek_block_bf();
void handle_opening_brace_token(Token token);
void handle_closing_brace_token(Token token, FILE* input_source);

// Utility & BSH Callers
char* trim_whitespace(char *str);
void free_all_variables();
void free_function_list();
void free_operator_list();
void free_loaded_libs();
long get_file_pos(FILE* f);
char* unescape_string(const char* input, char* output_buffer, size_t buffer_size);
bool input_source_is_file(FILE* f);
bool invoke_bsh_function_for_op(const char* func_name_to_call, // For binary dynamic ops
                                const char* arg1_val, const char* arg2_val, const char* arg3_op,
                                const char* bsh_result_var_name,
                                char* c_result_buffer, size_t c_result_buffer_size);
bool invoke_bsh_unary_op_call(const char* func_name_to_call, // For unary ops like $var++
                                const char* bsh_arg1_var_name_str, 
                                const char* bsh_arg2_result_holder_var_name,
                                char* c_result_buffer, size_t c_result_buffer_size);
bool is_comparison_or_assignment_operator(const char* op_str);

// object: management
void parse_and_flatten_bsh_object_string(const char* data_string, const char* base_var_name, const char* data_type_prefix, int current_scope_id);
bool stringify_bsh_object_to_string(const char* base_var_name, char* output_buffer, size_t buffer_size);

// --- Main ---
int main(int argc, char *argv[]) {
    initialize_shell();

    // Execute default startup script
    char startup_script_path[MAX_FULL_PATH_LEN];
    char* home_dir = getenv("HOME");
    bool startup_executed = false;
    if (home_dir) {
        snprintf(startup_script_path, sizeof(startup_script_path), "%s/%s", home_dir, DEFAULT_STARTUP_SCRIPT);
        if (access(startup_script_path, F_OK) == 0) {
            execute_script(startup_script_path, false, true); 
            startup_executed = true;
        }
    }
    if (!startup_executed) { 
         if (access(DEFAULT_STARTUP_SCRIPT, F_OK) == 0) {
            execute_script(DEFAULT_STARTUP_SCRIPT, false, true);
        }
    }

    if (argc > 1) { 
        execute_script(argv[1], false, false); 
    } else { // Interactive mode
        char line_buffer[INPUT_BUFFER_SIZE];
        char prompt_buffer[MAX_VAR_NAME_LEN + 30]; 
        int line_counter_interactive = 0;

        while (1) {
            char* current_prompt_val = get_variable_scoped("PS1");
            if (!current_prompt_val || strlen(current_prompt_val) == 0) {
                current_prompt_val = "bsh"; 
            }

            char state_indicator[35] = ""; 
            if (block_stack_top_bf >= 0) {
                BlockFrame* top_block = peek_block_bf();
                const char* block_type_str = "unknown";
                if (top_block) {
                    if (top_block->type == BLOCK_TYPE_IF) block_type_str = "if";
                    else if (top_block->type == BLOCK_TYPE_ELSE) block_type_str = "else";
                    else if (top_block->type == BLOCK_TYPE_WHILE) block_type_str = "while";
                    else if (top_block->type == BLOCK_TYPE_FUNCTION_DEF) block_type_str = "defunc_body";
                }

                if (current_exec_state == STATE_BLOCK_SKIP) {
                    snprintf(state_indicator, sizeof(state_indicator), "(skip %s %d)", block_type_str, block_stack_top_bf + 1);
                } else if (current_exec_state == STATE_DEFINE_FUNC_BODY && current_function_definition) {
                     snprintf(state_indicator, sizeof(state_indicator), "(defunc %s)", current_function_definition->name);
                } else if (top_block) { 
                    snprintf(state_indicator, sizeof(state_indicator), "(%s %d)", block_type_str, block_stack_top_bf + 1);
                }
            } else if (current_exec_state == STATE_DEFINE_FUNC_BODY && current_function_definition) {
                snprintf(state_indicator, sizeof(state_indicator), "(defunc %s...)", current_function_definition->name);
            }

            snprintf(prompt_buffer, sizeof(prompt_buffer), "%s%s> ", current_prompt_val, state_indicator);
            printf("%s", prompt_buffer);

            if (!fgets(line_buffer, sizeof(line_buffer), stdin)) {
                printf("\n"); 
                break;
            }
            line_counter_interactive++;
            process_line(line_buffer, stdin, line_counter_interactive, STATE_NORMAL);
        }
    }

    cleanup_shell();
    return 0;
}

// --- Core Implementations ---

// bsh.c (snippet)

// MODIFIED/RENAMED from invoke_bsh_function_for_op
// Can now be used for binary (op1, op2, op_str), 
// prefix (var_name, op_str, "prefix"), 
// or postfix (var_name, op_str, "postfix") calls to BSH __dynamic_op_handler.
bool invoke_bsh_dynamic_op_handler(
    const char* bsh_func_name_to_call, // Should be "__dynamic_op_handler"
    const char* arg1_val_or_var_name,  // For binary: operand1 value. For unary: variable name.
    const char* arg2_val_or_op_str,    // For binary: operand2 value. For unary prefix/postfix: operator string.
    const char* arg3_op_str_or_context,// For binary: operator string. For unary: "prefix" or "postfix" or type.
    const char* bsh_result_var_name,   // BSH variable where the BSH handler should store its result.
    char* c_result_buffer, size_t c_result_buffer_size) {

    UserFunction* func = function_list;
    while (func) {
        if (strcmp(func->name, bsh_func_name_to_call) == 0) break;
        func = func->next;
    }
    if (!func) {
        fprintf(stderr, "Error: BSH internal handler function '%s' not found.\n", bsh_func_name_to_call);
        snprintf(c_result_buffer, c_result_buffer_size, "NO_HANDLER_ERROR");
        return false;
    }

    // __dynamic_op_handler is expected to handle various argument counts or use placeholders.
    // Let's assume it expects 4 arguments: (arg1, arg2, arg3/op, result_var_name)
    // For unary: arg1=var_name, arg2=op_str, arg3="prefix"/"postfix", result_var_name
    // For binary: arg1=val1, arg2=val2, arg3=op_str, result_var_name
    if (func->param_count != 4) {
         fprintf(stderr, "Error: BSH function '%s' has incorrect param count (expected 4, got %d) for dynamic op handling.\n", bsh_func_name_to_call, func->param_count);
         snprintf(c_result_buffer, c_result_buffer_size, "HANDLER_PARAM_ERROR");
        return false;
    }

    Token call_tokens[4];
    char token_storage_arg1[INPUT_BUFFER_SIZE];
    char token_storage_arg2[INPUT_BUFFER_SIZE];
    char token_storage_arg3[INPUT_BUFFER_SIZE]; // op_str or context
    char token_storage_arg4_res_var[MAX_VAR_NAME_LEN];

    // Argument 1
    strncpy(token_storage_arg1, arg1_val_or_var_name, INPUT_BUFFER_SIZE -1); token_storage_arg1[INPUT_BUFFER_SIZE-1] = '\0';
    call_tokens[0].type = TOKEN_STRING; // Pass as string, BSH handler will know if it's a var name or value
    call_tokens[0].text = token_storage_arg1;
    call_tokens[0].len = strlen(token_storage_arg1);

    // Argument 2
    strncpy(token_storage_arg2, arg2_val_or_op_str, INPUT_BUFFER_SIZE -1); token_storage_arg2[INPUT_BUFFER_SIZE-1] = '\0';
    call_tokens[1].type = TOKEN_STRING;
    call_tokens[1].text = token_storage_arg2;
    call_tokens[1].len = strlen(token_storage_arg2);
    
    // Argument 3
    strncpy(token_storage_arg3, arg3_op_str_or_context, INPUT_BUFFER_SIZE -1); token_storage_arg3[INPUT_BUFFER_SIZE-1] = '\0';
    call_tokens[2].type = TOKEN_STRING;
    call_tokens[2].text = token_storage_arg3;
    call_tokens[2].len = strlen(token_storage_arg3);

    // Argument 4 (result holder variable name)
    strncpy(token_storage_arg4_res_var, bsh_result_var_name, MAX_VAR_NAME_LEN -1); token_storage_arg4_res_var[MAX_VAR_NAME_LEN-1] = '\0';
    call_tokens[3].type = TOKEN_WORD; // Name of variable to set
    call_tokens[3].text = token_storage_arg4_res_var;
    call_tokens[3].len = strlen(token_storage_arg4_res_var);

    execute_user_function(func, call_tokens, 4, NULL);

    char* result_from_bsh = get_variable_scoped(bsh_result_var_name);
    if (result_from_bsh) {
        strncpy(c_result_buffer, result_from_bsh, c_result_buffer_size - 1);
        c_result_buffer[c_result_buffer_size - 1] = '\0';
    } else {
        snprintf(c_result_buffer, c_result_buffer_size, "OP_HANDLER_NO_RESULT_VAR<%s>", bsh_result_var_name);
        // This might be an error or might be acceptable if the operation has side effects only
        // and doesn't produce a distinct "expression value" (e.g. some BSH handler designs).
        // For typical math ops or assignments, a result is expected.
    }
    return true;
}

void process_line(char *line_raw, FILE *input_source, int current_line_no, ExecutionState exec_mode_param) {
    char line[MAX_LINE_LENGTH];
    strncpy(line, line_raw, MAX_LINE_LENGTH -1);
    line[MAX_LINE_LENGTH-1] = '\0';
    trim_whitespace(line);

    if (line[0] == '\0') { 
        return;
    }

    if (is_defining_function && current_function_definition &&
        (current_exec_state == STATE_DEFINE_FUNC_BODY || current_exec_state == STATE_IMPORT_PARSING || exec_mode_param == STATE_IMPORT_PARSING) &&
        block_stack_top_bf >=0 && peek_block_bf() && peek_block_bf()->type == BLOCK_TYPE_FUNCTION_DEF && 
        strncmp(line, "}", 1) != 0 && strncmp(line, "}", strlen(line)) != 0 ) { 

        if (current_function_definition->line_count < MAX_FUNC_LINES) {
            current_function_definition->body[current_function_definition->line_count] = strdup(line);
            if (!current_function_definition->body[current_function_definition->line_count]) {
                perror("strdup for function body line failed");
            } else {
                current_function_definition->line_count++;
            }
        } else {
            fprintf(stderr, "Error: Function '%s' exceeds maximum line count of %d.\n",
                    current_function_definition->name, MAX_FUNC_LINES);
             for(int i=0; i < current_function_definition->line_count; ++i) if(current_function_definition->body[i]) free(current_function_definition->body[i]);
            free(current_function_definition);
            current_function_definition = NULL;
            is_defining_function = false;
            if (block_stack_top_bf >=0 && peek_block_bf()->type == BLOCK_TYPE_FUNCTION_DEF) pop_block_bf(); 
            current_exec_state = STATE_NORMAL; 
        }
        return; 
    }


    Token tokens[MAX_ARGS];
    char token_storage[TOKEN_STORAGE_SIZE];
    int num_tokens = advanced_tokenize_line(line, tokens, MAX_ARGS, token_storage, TOKEN_STORAGE_SIZE);

    if (num_tokens == 0 || tokens[0].type == TOKEN_EMPTY || tokens[0].type == TOKEN_EOF) {
        return; 
    }

    if (tokens[0].type == TOKEN_COMMENT) {
        return; 
    }

    if (tokens[0].type == TOKEN_LBRACE && num_tokens == 1) { 
        handle_opening_brace_token(tokens[0]);
        return;
    }
    if (tokens[0].type == TOKEN_RBRACE && num_tokens == 1) { 
        handle_closing_brace_token(tokens[0], input_source);
        return;
    }
    if (num_tokens > 1 && tokens[num_tokens-1].type == TOKEN_LBRACE) {
        // Let specific handlers consume it.
    }
    if (num_tokens > 1 && tokens[num_tokens-1].type == TOKEN_RBRACE) {
        if (tokens[0].type == TOKEN_RBRACE && (tokens[1].type == TOKEN_COMMENT || tokens[1].type == TOKEN_EOF) ){
             handle_closing_brace_token(tokens[0], input_source);
             return;
        }
    }

    if (current_exec_state == STATE_BLOCK_SKIP && exec_mode_param != STATE_IMPORT_PARSING) {
        const char* first_token_text_resolved = NULL;
        if (tokens[0].type == TOKEN_WORD) {
             first_token_text_resolved = resolve_keyword_alias(tokens[0].text);
        }

        if (tokens[0].type == TOKEN_RBRACE) { 
            handle_closing_brace_token(tokens[0], input_source);
        } else if (first_token_text_resolved &&
                   (strcmp(first_token_text_resolved, "else") == 0 )) {
            handle_else_statement_advanced(tokens, num_tokens, input_source, current_line_no);
        } else if (first_token_text_resolved && strcmp(first_token_text_resolved, "if") == 0){
            push_block_bf(BLOCK_TYPE_IF, false, 0, current_line_no);
        } else if (first_token_text_resolved && strcmp(first_token_text_resolved, "while") == 0){
            push_block_bf(BLOCK_TYPE_WHILE, false, 0, current_line_no);
        } else if (first_token_text_resolved && strcmp(first_token_text_resolved, "defunc") == 0){
             push_block_bf(BLOCK_TYPE_FUNCTION_DEF, false, 0, current_line_no); 
        } else if (tokens[0].type == TOKEN_LBRACE) { 
            BlockFrame* current_block = peek_block_bf();
            if (!current_block) { // Should not happen if LBRACE follows a skipped if/while/defunc
                 fprintf(stderr, "Syntax error: Unmatched '{' on line %d while skipping.\n", current_line_no);
            }
        }        
        return;
    }


    // --- Actual command/statement processing ---
    if (tokens[0].type == TOKEN_VARIABLE && num_tokens > 1 && tokens[1].type == TOKEN_ASSIGN) {
        handle_assignment_advanced(tokens, num_tokens);
    } 
    // Check for unary prefix/postfix operations (e.g. $var++, ++$var)
    // These should have exactly 2 tokens if there are no spaces.
    else if (num_tokens == 2 &&
               tokens[0].type == TOKEN_VARIABLE &&
               tokens[1].type == TOKEN_OPERATOR &&
               (strcmp(tokens[1].text, "++") == 0 || strcmp(tokens[1].text, "--") == 0)) {
        // Postfix: $var++ or $var--
        handle_unary_op_statement(&tokens[0], &tokens[1], false /*is_prefix*/);
    } else if (num_tokens == 2 &&
               tokens[1].type == TOKEN_VARIABLE && // Operand is the second token
               tokens[0].type == TOKEN_OPERATOR &&
               (strcmp(tokens[0].text, "++") == 0 || strcmp(tokens[0].text, "--") == 0)) {
        // Prefix: ++$var or --$var
        handle_unary_op_statement(&tokens[1], &tokens[0], true /*is_prefix*/);
    }
    else if (tokens[0].type == TOKEN_WORD) {
        const char* command_name = resolve_keyword_alias(tokens[0].text);

        if (strcmp(command_name, "echo") == 0) {
            handle_echo_advanced(tokens, num_tokens);
        } else if (strcmp(command_name, "defkeyword") == 0) {
            handle_defkeyword_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "if") == 0) {
            handle_if_statement_advanced(tokens, num_tokens, input_source, current_line_no);
        } else if (strcmp(command_name, "else") == 0) { 
            handle_else_statement_advanced(tokens, num_tokens, input_source, current_line_no);
        } else if (strcmp(command_name, "while") == 0) {
            handle_while_statement_advanced(tokens, num_tokens, input_source, current_line_no);
        } else if (strcmp(command_name, "defunc") == 0) {
            handle_defunc_statement_advanced(tokens, num_tokens);
        } else if (strcmp(command_name, "inc") == 0) {
            handle_inc_dec_statement_advanced(tokens, num_tokens, true);
        } else if (strcmp(command_name, "dec") == 0) {
            handle_inc_dec_statement_advanced(tokens, num_tokens, false);
        } else if (strcmp(command_name, "loadlib") == 0) {
            handle_loadlib_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "calllib") == 0) {
            handle_calllib_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "import") == 0) {
            handle_import_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "defoperator") == 0) {
            handle_defoperator_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "update_cwd") == 0) {
            handle_update_cwd_statement(tokens, num_tokens);        
        } else if (strcmp(command_name, "eval") == 0) { 
            handle_eval_statement(tokens, num_tokens);
        } else if (strcmp(command_name, "exit") == 0) {
            handle_exit_statement(tokens, num_tokens);
        }
        else {            
            UserFunction* func_to_run = function_list;
            bool found_user_func = false;
            while(func_to_run) {
                if (strcmp(func_to_run->name, command_name) == 0) {
                    found_user_func = true;
                    break;
                }
                func_to_run = func_to_run->next;
            }

            if (found_user_func) {
                execute_user_function(func_to_run, &tokens[1], num_tokens - 1, input_source);
            } else {
                // Standalone dynamic operator pattern: val1 op val2 (e.g. 10 + 5 at prompt)
                if ( (num_tokens == 3 || (num_tokens == 4 && tokens[3].type == TOKEN_COMMENT)) &&
                     (tokens[0].type == TOKEN_VARIABLE || tokens[0].type == TOKEN_NUMBER || tokens[0].type == TOKEN_STRING || tokens[0].type == TOKEN_WORD) && 
                     tokens[1].type == TOKEN_OPERATOR && !is_comparison_or_assignment_operator(tokens[1].text) &&
                     (tokens[2].type == TOKEN_VARIABLE || tokens[2].type == TOKEN_NUMBER || tokens[2].type == TOKEN_STRING || tokens[2].type == TOKEN_WORD) 
                   ) {
                        char op1_expanded[INPUT_BUFFER_SIZE];
                        char op2_expanded[INPUT_BUFFER_SIZE];
                        char result_c_buffer[INPUT_BUFFER_SIZE];
                        const char* operator_str = tokens[1].text;
                        const char* temp_bsh_result_var = "__TEMP_STANDALONE_OP_RES"; 

                        if (tokens[0].type == TOKEN_STRING) {
                            char unescaped[INPUT_BUFFER_SIZE];
                            unescape_string(tokens[0].text, unescaped, sizeof(unescaped));
                            expand_variables_in_string_advanced(unescaped, op1_expanded, sizeof(op1_expanded));
                        } else { 
                            expand_variables_in_string_advanced(tokens[0].text, op1_expanded, sizeof(op1_expanded));
                        }

                        if (tokens[2].type == TOKEN_STRING) {
                            char unescaped[INPUT_BUFFER_SIZE];
                            unescape_string(tokens[2].text, unescaped, sizeof(unescaped));
                            expand_variables_in_string_advanced(unescaped, op2_expanded, sizeof(op2_expanded));
                        } else { 
                            expand_variables_in_string_advanced(tokens[2].text, op2_expanded, sizeof(op2_expanded));
                        }
                        
                        if (invoke_bsh_function_for_op("__dynamic_op_handler",
                                                       op1_expanded, op2_expanded, operator_str, 
                                                       temp_bsh_result_var,
                                                       result_c_buffer, sizeof(result_c_buffer))) {
                            if (strlen(result_c_buffer) > 0 &&
                                strncmp(result_c_buffer, "OP_HANDLER_NO_RESULT_VAR", 26) != 0 &&
                                strncmp(result_c_buffer, "NO_HANDLER_ERROR", 16) != 0 &&
                                strncmp(result_c_buffer, "UNKNOWN_HANDLER_ERROR", 21) != 0 ) {
                                printf("%s\n", result_c_buffer); // Print result of standalone operation
                            }
                            set_variable_scoped("LAST_OP_RESULT", result_c_buffer, false);
                        } else {
                            fprintf(stderr, "Error executing standalone dynamic operation for: %s %s %s\n", op1_expanded, operator_str, op2_expanded);
                            set_variable_scoped("LAST_OP_RESULT", "STANDALONE_OP_ERROR", false);
                        }
                } else {
                    // New: Check for standalone unary prefix operations: e.g., ++$var or --$var
                    if (num_tokens == 2 &&
                        tokens[0].type == TOKEN_OPERATOR &&
                        (strcmp(tokens[0].text, "++") == 0 || strcmp(tokens[0].text, "--") == 0) &&
                        tokens[1].type == TOKEN_VARIABLE) {
                        
                        char var_name_clean[MAX_VAR_NAME_LEN];
                        // Extract clean variable name from tokens[1].text (e.g., "$myvar" -> "myvar")
                        // This logic needs to be robust for $var, ${var}
                        if (tokens[1].text[0] == '$') {
                            if (tokens[1].text[1] == '{') {
                                const char* end_brace = strchr(tokens[1].text + 2, '}');
                                if (end_brace && (end_brace - (tokens[1].text + 2) < MAX_VAR_NAME_LEN)) {
                                    strncpy(var_name_clean, tokens[1].text + 2, end_brace - (tokens[1].text + 2));
                                    var_name_clean[end_brace - (tokens[1].text + 2)] = '\0';
                                } else { /* error or malformed */ strcpy(var_name_clean, ""); }
                            } else {
                                strncpy(var_name_clean, tokens[1].text + 1, MAX_VAR_NAME_LEN - 1);
                                var_name_clean[MAX_VAR_NAME_LEN - 1] = '\0';
                            }
                            // Note: Array element unary ops like ++$arr[idx] are complex to parse here simply.
                            // The BSH __dynamic_op_handler would need to handle var_name_clean if it's "arr[idx]".
                        } else { strcpy(var_name_clean, ""); /* Should not happen if TOKEN_VARIABLE */ }


                        if (strlen(var_name_clean) > 0) {
                            char result_c_buffer[INPUT_BUFFER_SIZE];
                            const char* temp_bsh_result_var = "__TEMP_STANDALONE_OP_RES";
                            
                            // Call BSH __dynamic_op_handler: (var_name, op_str, "prefix", result_holder)
                            if (invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                                        var_name_clean,         // Variable Name
                                                        tokens[0].text,         // Operator "++" or "--"
                                                        "prefix",               // Context
                                                        temp_bsh_result_var,
                                                        result_c_buffer, sizeof(result_c_buffer))) {
                                if (strlen(result_c_buffer) > 0 && strncmp(result_c_buffer, "OP_HANDLER_NO_RESULT_VAR", 26) != 0) {
                                    printf("%s\n", result_c_buffer); // Print result of standalone prefix op
                                }
                                set_variable_scoped("LAST_OP_RESULT", result_c_buffer, false);
                            } else {
                                fprintf(stderr, "Error executing standalone prefix operation for: %s %s\n", tokens[0].text, var_name_clean);
                                set_variable_scoped("LAST_OP_RESULT", "STANDALONE_OP_ERROR", false);
                            }
                        } else {
                            fprintf(stderr, "Error: Malformed variable for prefix operation: %s\n", tokens[1].text);
                        }

                    } // Check for standalone unary postfix operations: e.g., $var++ or $var--
                    else if (num_tokens == 2 &&
                            tokens[0].type == TOKEN_VARIABLE &&
                            tokens[1].type == TOKEN_OPERATOR &&
                            (strcmp(tokens[1].text, "++") == 0 || strcmp(tokens[1].text, "--") == 0)) {

                        char var_name_clean[MAX_VAR_NAME_LEN];
                        // Extract clean variable name from tokens[0].text
                        if (tokens[0].text[0] == '$') {
                            if (tokens[0].text[1] == '{') {
                                // Similar to prefix block
                                const char* end_brace = strchr(tokens[0].text + 2, '}');
                                if (end_brace && (end_brace - (tokens[0].text + 2) < MAX_VAR_NAME_LEN)) {
                                    strncpy(var_name_clean, tokens[0].text + 2, end_brace - (tokens[0].text + 2));
                                    var_name_clean[end_brace - (tokens[0].text + 2)] = '\0';
                                } else { strcpy(var_name_clean, ""); }
                            } else {
                                strncpy(var_name_clean, tokens[0].text + 1, MAX_VAR_NAME_LEN - 1);
                                var_name_clean[MAX_VAR_NAME_LEN - 1] = '\0';
                            }
                        } else { strcpy(var_name_clean, "");}

                        if (strlen(var_name_clean) > 0) {
                            char result_c_buffer[INPUT_BUFFER_SIZE];
                            const char* temp_bsh_result_var = "__TEMP_STANDALONE_OP_RES";

                            // Call BSH __dynamic_op_handler: (var_name, op_str, "postfix", result_holder)
                            if (invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                                        var_name_clean,         // Variable Name
                                                        tokens[1].text,         // Operator "++" or "--"
                                                        "postfix",              // Context
                                                        temp_bsh_result_var,
                                                        result_c_buffer, sizeof(result_c_buffer))) {
                                if (strlen(result_c_buffer) > 0 && strncmp(result_c_buffer, "OP_HANDLER_NO_RESULT_VAR", 26) != 0) {
                                    printf("%s\n", result_c_buffer); // Print result of standalone postfix op
                                }
                                set_variable_scoped("LAST_OP_RESULT", result_c_buffer, false);
                            } else {
                                fprintf(stderr, "Error executing standalone postfix operation for: %s %s\n", var_name_clean, tokens[1].text);
                                set_variable_scoped("LAST_OP_RESULT", "STANDALONE_OP_ERROR", false);
                            }
                        } else {
                            fprintf(stderr, "Error: Malformed variable for postfix operation: %s\n", tokens[0].text);
                        }
                    }
                    // Standalone dynamic binary operator pattern: val1 op val2 (e.g. 10 + 5 at prompt)
                    // This was the existing logic.
                    else if ( (num_tokens == 3 || (num_tokens == 4 && tokens[3].type == TOKEN_COMMENT)) &&
                        (tokens[0].type == TOKEN_VARIABLE || tokens[0].type == TOKEN_NUMBER || tokens[0].type == TOKEN_STRING || tokens[0].type == TOKEN_WORD) && 
                        tokens[1].type == TOKEN_OPERATOR && !is_comparison_or_assignment_operator(tokens[1].text) && // keep this check
                        (tokens[2].type == TOKEN_VARIABLE || tokens[2].type == TOKEN_NUMBER || tokens[2].type == TOKEN_STRING || tokens[2].type == TOKEN_WORD) 
                    ) {
                            char op1_expanded[INPUT_BUFFER_SIZE];
                            char op2_expanded[INPUT_BUFFER_SIZE];
                            char result_c_buffer[INPUT_BUFFER_SIZE];
                            const char* operator_str = tokens[1].text;
                            const char* temp_bsh_result_var = "__TEMP_STANDALONE_OP_RES"; 

                            // ... (expansion of op1_expanded, op2_expanded as before)
                            if (tokens[0].type == TOKEN_STRING) { /* ... */ } else { /* ... */ }
                            expand_variables_in_string_advanced(tokens[0].text, op1_expanded, sizeof(op1_expanded)); // Simplified for snippet
                            if (tokens[2].type == TOKEN_STRING) { /* ... */ } else { /* ... */ }
                            expand_variables_in_string_advanced(tokens[2].text, op2_expanded, sizeof(op2_expanded)); // Simplified for snippet
                            
                            // Call BSH __dynamic_op_handler: (val1, val2, op_str, result_holder)
                            if (invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                                        op1_expanded, op2_expanded, operator_str, 
                                                        temp_bsh_result_var,
                                                        result_c_buffer, sizeof(result_c_buffer))) {
                                if (strlen(result_c_buffer) > 0 &&
                                    strncmp(result_c_buffer, "OP_HANDLER_NO_RESULT_VAR", 26) != 0 &&
                                    /* ... other error checks ... */ ) {
                                    printf("%s\n", result_c_buffer); 
                                }
                                set_variable_scoped("LAST_OP_RESULT", result_c_buffer, false);
                            } else {
                                fprintf(stderr, "Error executing standalone dynamic binary operation for: %s %s %s\n", op1_expanded, operator_str, op2_expanded);
                                set_variable_scoped("LAST_OP_RESULT", "STANDALONE_OP_ERROR", false);
                            }
                    } else {

                        // Try to evaluate the whole line as an expression if it's not assignment/command.
                        char expression_result_buffer[INPUT_BUFFER_SIZE];
                        if (evaluate_expression_tokens(tokens, 0, num_tokens - 1, expression_result_buffer, sizeof(expression_result_buffer))) {
                            if (strlen(expression_result_buffer) > 0 &&
                                strncmp(expression_result_buffer, "TERNARY_COND_EVAL_ERROR", strlen("TERNARY_COND_EVAL_ERROR")) != 0 &&
                                strncmp(expression_result_buffer, "EXPR_EVAL_ERROR", strlen("EXPR_EVAL_ERROR")) != 0 &&
                                // Also check against results from invoke_bsh_dynamic_op_handler if they indicate errors
                                strncmp(expression_result_buffer, "OP_HANDLER_NO_RESULT_VAR", strlen("OP_HANDLER_NO_RESULT_VAR")) !=0 &&
                                strncmp(expression_result_buffer, "NO_HANDLER_ERROR", strlen("NO_HANDLER_ERROR")) !=0 &&
                                strncmp(expression_result_buffer, "UNKNOWN_HANDLER_ERROR", strlen("UNKNOWN_HANDLER_ERROR")) !=0 &&
                                strncmp(expression_result_buffer, "UNKNOWN_PREFIX_OP_ERROR", strlen("UNKNOWN_PREFIX_OP_ERROR")) !=0 &&
                                strncmp(expression_result_buffer, "UNARY_PREFIX_OP_ERROR", strlen("UNARY_PREFIX_OP_ERROR")) !=0 &&
                                // ... etc. for other error strings from invoke_bsh_dynamic_op_handler or its BSH callees
                                true /* add more positive checks if needed, or fewer error checks */
                                ) {
                                printf("%s\n", expression_result_buffer);
                            }
                            set_variable_scoped("LAST_OP_RESULT", expression_result_buffer, false); // Or a new var like LAST_EXPR_RESULT
                        } else {
                            // If evaluate_expression_tokens returned false, it's a more fundamental parsing error
                            // OR it could be an external command if no expression pattern matched.
                            // This 'else' branch would now contain the logic to try it as an external command.
                            char command_path_ext[MAX_FULL_PATH_LEN];
                            if (find_command_in_path_dynamic(tokens[0].text, command_path_ext)) {
                                // External command
                                char command_path[MAX_FULL_PATH_LEN];
                                if (find_command_in_path_dynamic(command_name, command_path)) {
                                    char *args[MAX_ARGS + 1];
                                    char expanded_args_storage[MAX_ARGS][INPUT_BUFFER_SIZE];
                                    args[0] = command_path; 
                                    int arg_count = 1;

                                    for (int i = 1; i < num_tokens; ++i) {
                                        if (tokens[i].type == TOKEN_COMMENT) break; 
                                        if (arg_count < MAX_ARGS) {
                                            if (tokens[i].type == TOKEN_STRING) {
                                                char unescaped_val[INPUT_BUFFER_SIZE];
                                                unescape_string(tokens[i].text, unescaped_val, sizeof(unescaped_val));
                                                expand_variables_in_string_advanced(unescaped_val, expanded_args_storage[arg_count-1], INPUT_BUFFER_SIZE);
                                            } else {
                                                expand_variables_in_string_advanced(tokens[i].text, expanded_args_storage[arg_count-1], INPUT_BUFFER_SIZE);
                                            }
                                            args[arg_count++] = expanded_args_storage[arg_count-1];
                                        } else {
                                            fprintf(stderr, "Warning: Too many arguments for command '%s'. Max %d allowed.\n", command_name, MAX_ARGS);
                                            break;
                                        }
                                    }
                                    args[arg_count] = NULL; 

                                    execute_external_command(command_path, args, arg_count, NULL, 0); 
                                } else {
                                    fprintf(stderr, "Command not found: %s (line %d)\n", command_name, current_line_no);
                                }
                            } else {
                                fprintf(stderr, "Command not found or syntax error: %s\n", tokens[0].text);
                            }
                        }                        
                    }
                }
            }
        }
    } else if (tokens[0].type == TOKEN_LBRACE) { 
        handle_opening_brace_token(tokens[0]); 
        if (num_tokens > 1 && tokens[1].type != TOKEN_COMMENT && tokens[1].type != TOKEN_EOF) {
            fprintf(stderr, "Warning: Tokens found after '{' on the same line %d. '{' should ideally be standalone or at the end of if/while/defunc.\n", current_line_no);
        }
    } else if (tokens[0].type == TOKEN_RBRACE) { 
         handle_closing_brace_token(tokens[0], input_source);
         if (num_tokens > 1 && tokens[1].type != TOKEN_COMMENT && tokens[1].type != TOKEN_EOF) {
             fprintf(stderr, "Warning: Tokens found after '}' on the same line %d. '}' should ideally be standalone.\n", current_line_no);
         }
    } else {
        fprintf(stderr, "Syntax error or unknown command starting with '%s' (type %d) on line %d.\n", tokens[0].text, tokens[0].type, current_line_no);
    }
}


// Shell
void initialize_shell() {
    scope_stack_top = -1; 
    enter_scope();        

    char *path_env = getenv("PATH");
    if (path_env) {
        char *path_copy = strdup(path_env);
        if (path_copy) {
            char *token_path = strtok(path_copy, ":");
            while (token_path) {
                add_path_to_list(&path_list_head, token_path);
                token_path = strtok(NULL, ":");
            }
            free(path_copy);
        } else { perror("strdup for PATH failed in initialize_shell"); }
    }

    initialize_module_path(); 
    initialize_operators_dynamic(); 

    set_variable_scoped("SHELL_VERSION", "bsh-dynamic-vals-0.6", false); // Updated version
    set_variable_scoped("PS1", "bsh", false); 

    char* initial_module_path_env = getenv("BSH_MODULE_PATH");
    if (!initial_module_path_env || strlen(initial_module_path_env) == 0) {
        initial_module_path_env = DEFAULT_MODULE_PATH;
    }
    set_variable_scoped("BSH_MODULE_PATH", initial_module_path_env, false);
    
    char cwd_buffer[PATH_MAX];
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) != NULL) {
        set_variable_scoped("CWD", cwd_buffer, false);
    } else {
        perror("bsh: getcwd() error on init");
        set_variable_scoped("CWD", "", false); 
    }
}

void cleanup_shell() {
    free_all_variables();
    free_function_list();
    free_operator_list();
    free_keyword_alias_list();
    free_path_dir_list(&path_list_head);
    free_path_dir_list(&module_path_list_head);
    free_loaded_libs();

    while(scope_stack_top >= 0) { 
        leave_scope(scope_stack[scope_stack_top].scope_id);
    }
}

// --- Path Management ---
void add_path_to_list(PathDirNode **list_head, const char* dir_path) {
    PathDirNode *new_node = (PathDirNode*)malloc(sizeof(PathDirNode));
    if (!new_node) { perror("malloc for path node failed"); return; }
    new_node->path = strdup(dir_path);
    if (!new_node->path) { perror("strdup for path string failed"); free(new_node); return; }
    new_node->next = NULL;

    if (!*list_head) { *list_head = new_node; }
    else { PathDirNode *current = *list_head; while (current->next) current = current->next; current->next = new_node; }
}

void free_path_dir_list(PathDirNode **list_head) {
    PathDirNode *current = *list_head;
    PathDirNode *next_node;
    while (current) {
        next_node = current->next;
        free(current->path);
        free(current);
        current = next_node;
    }
    *list_head = NULL;
}

void initialize_module_path() {
    char *module_path_env = getenv("BSH_MODULE_PATH");
    char *effective_module_path = module_path_env;

    if (!module_path_env || strlen(module_path_env) == 0) {
        effective_module_path = DEFAULT_MODULE_PATH;
    }

    if (effective_module_path && strlen(effective_module_path) > 0) {
        char *path_copy = strdup(effective_module_path);
        if (path_copy) {
            char *token_path = strtok(path_copy, ":");
            while (token_path) {
                if(strlen(token_path) > 0) add_path_to_list(&module_path_list_head, token_path);
                token_path = strtok(NULL, ":");
            }
            free(path_copy);
        } else { perror("strdup for BSH_MODULE_PATH processing failed"); }
    }
}

// --- Tokenizer & Keyword Aliasing ---

void initialize_operators_dynamic() {
    // Only define absolutely essential structural operators/tokens in C.
    // All other "operational" symbols (+, *, ++, --, .) will be defined by BSH scripts
    // using the 'defoperator' built-in command.

    add_operator_dynamic("=", TOKEN_ASSIGN);   // Essential for assignment
    add_operator_dynamic("(", TOKEN_LPAREN);   // Essential for grouping/syntax
    add_operator_dynamic(")", TOKEN_RPAREN);
    add_operator_dynamic("{", TOKEN_LBRACE);   // Essential for blocks
    add_operator_dynamic("}", TOKEN_RBRACE);
    add_operator_dynamic("[", TOKEN_LBRACKET); // Essential for array syntax
    add_operator_dynamic("]", TOKEN_RBRACKET);
    add_operator_dynamic(";", TOKEN_SEMICOLON); // Command separator
    add_operator_dynamic("|", TOKEN_PIPE);      // Pipe
    add_operator_dynamic("&", TOKEN_AMPERSAND);  // Background

    // Note: "!", "&&", "||" could also be here if considered core syntactic elements
    // for conditions, or they too could be defined by scripts. For now, let's
    // assume they might be common enough for C, or can be added by init.bsh.
    // Let's keep it minimal for this example.
    // Comparison operators like "==" will be defined by scripts via 'defoperator'.

    add_operator_dynamic("?", TOKEN_QMARK);
    add_operator_dynamic(":", TOKEN_COLON);
}

void add_operator_dynamic(const char* op_str, TokenType type) {
    if (strlen(op_str) > MAX_OPERATOR_LEN) {
        fprintf(stderr, "Warning: Operator '%s' too long (max %d chars).\n", op_str, MAX_OPERATOR_LEN);
        return;
    }
    OperatorDefinition *new_op = (OperatorDefinition*)malloc(sizeof(OperatorDefinition));
    if (!new_op) { perror("malloc for new operator failed"); return; }
    strcpy(new_op->op_str, op_str); new_op->op_type = type;
    new_op->next = operator_list_head; operator_list_head = new_op; 
}

int match_operator_dynamic(const char *input, const char **op_text, TokenType *matched_type) {
    OperatorDefinition *current = operator_list_head;
    const char* best_match_text = NULL;
    TokenType best_match_type = TOKEN_EMPTY; 
    int longest_match_len = 0;

    while (current) {
        size_t op_len = strlen(current->op_str);
        if (strncmp(input, current->op_str, op_len) == 0) {
            if (op_len > longest_match_len) {
                longest_match_len = op_len;
                best_match_text = current->op_str; 
                best_match_type = current->op_type;
            }
        }
        current = current->next;
    }

    if (longest_match_len > 0) {
        *op_text = best_match_text; 
        if(matched_type) *matched_type = best_match_type;
        return longest_match_len;
    }
    return 0; 
}

void add_keyword_alias(const char* original, const char* alias_name) {
    if (strlen(original) > MAX_KEYWORD_LEN || strlen(alias_name) > MAX_KEYWORD_LEN) {
        fprintf(stderr, "Keyword or alias too long (max %d chars).\n", MAX_KEYWORD_LEN); return;
    }
    KeywordAlias* current = keyword_alias_head;
    while(current){ 
        if(strcmp(current->alias, alias_name) == 0){
            fprintf(stderr, "Warning: Alias '%s' already defined for '%s'. Overwriting with new original '%s'.\n", alias_name, current->original, original);
            strncpy(current->original, original, MAX_KEYWORD_LEN); 
            current->original[MAX_KEYWORD_LEN] = '\0';
            return;
        }
        current = current->next;
    }
    KeywordAlias *new_alias = (KeywordAlias*)malloc(sizeof(KeywordAlias));
    if (!new_alias) { perror("malloc for keyword alias failed"); return; }
    strncpy(new_alias->original, original, MAX_KEYWORD_LEN); new_alias->original[MAX_KEYWORD_LEN] = '\0';
    strncpy(new_alias->alias, alias_name, MAX_KEYWORD_LEN); new_alias->alias[MAX_KEYWORD_LEN] = '\0';
    new_alias->next = keyword_alias_head; keyword_alias_head = new_alias; 
}

const char* resolve_keyword_alias(const char* alias_name) {
    KeywordAlias *current = keyword_alias_head;
    while (current) {
        if (strcmp(current->alias, alias_name) == 0) {
            return current->original; 
        }
        current = current->next;
    }
    return alias_name; 
}

void free_keyword_alias_list() {
    KeywordAlias *current = keyword_alias_head; KeywordAlias *next_ka;
    while (current) { next_ka = current->next; free(current); current = next_ka; }
    keyword_alias_head = NULL;
}

int advanced_tokenize_line(const char *line, Token *tokens, int max_tokens, char *token_storage, size_t storage_size) {
    int token_count = 0;
    const char *p = line;
    char *storage_ptr = token_storage;
    size_t remaining_storage = storage_size;

    while (*p && token_count < max_tokens) {
        while (isspace((unsigned char)*p)) p++; // Skip leading whitespace
        if (!*p) break; // End of line

        const char *p_token_start = p; // Mark the beginning of the potential token

        // 1. Handle Comments
        if (*p == '#') {
            tokens[token_count].type = TOKEN_COMMENT;
            tokens[token_count].text = p_token_start; // Store from '#'
            tokens[token_count].len = strlen(p_token_start);
            p += tokens[token_count].len; // Consume the rest of the line
            // No need to copy to token_storage if text points directly to line
            // but if copying: strncpy(storage_ptr, p_token_start, tokens[token_count].len); ...
            token_count++;
            break; // Comment ends line processing for tokens
        }

        // 2. Attempt to Parse as TOKEN_NUMBER (handles negative, positive, float)
        // This logic needs to be comprehensive.
        const char *p_after_num_parse = p; // Will be updated if a number is parsed
        bool is_num = false;
        // --- BEGIN Number Parsing Logic ---
        bool is_negative_num = false;
        const char* p_current_num_char = p;
        if (*p_current_num_char == '-') {
            is_negative_num = true;
            p_current_num_char++;
        }

        const char* p_num_digits_start = p_current_num_char; // Where digits/decimal point should start

        // Check for digits before decimal or just decimal point if starting with it (e.g. .5)
        if (isdigit((unsigned char)*p_current_num_char) || (*p_current_num_char == '.' && isdigit((unsigned char)*(p_current_num_char + 1)))) {
            if (*p_current_num_char != '.') { // If not starting with '.', consume integer part
                while (isdigit((unsigned char)*p_current_num_char)) {
                    p_current_num_char++;
                }
            }
            if (*p_current_num_char == '.') { // Consume decimal and fractional part
                p_current_num_char++; // Consume '.'
                while (isdigit((unsigned char)*p_current_num_char)) {
                    p_current_num_char++;
                }
            }
            // Ensure at least one digit was part of the number structure
            // (e.g. prevent "-" or "-." from being TOKEN_NUMBER)
            bool has_digits = false;
            for (const char* ch = p_num_digits_start; ch < p_current_num_char; ++ch) {
                if (isdigit((unsigned char)*ch)) { has_digits = true; break; }
            }

            if (has_digits) {
                is_num = true;
                p_after_num_parse = p_current_num_char;
            }
        }
        // --- END Number Parsing Logic ---

        if (is_num) {
            tokens[token_count].type = TOKEN_NUMBER;
            tokens[token_count].text = storage_ptr;
            tokens[token_count].len = p_after_num_parse - p_token_start;
            // ... (copy to storage_ptr, increment storage_ptr, decrement remaining_storage, check bounds) ...
            strncpy(storage_ptr, p_token_start, tokens[token_count].len);
            storage_ptr[tokens[token_count].len] = '\0';
            storage_ptr += (tokens[token_count].len + 1);
            remaining_storage -= (tokens[token_count].len + 1);

            p = p_after_num_parse;
            token_count++;
            continue;
        }
        // p remains p_token_start if not a number

        // 3. Attempt to Match Script-Defined Operators
        const char *matched_op_text = NULL;
        TokenType matched_op_type = TOKEN_OPERATOR;
        int op_len = match_operator_dynamic(p, &matched_op_text, &matched_op_type);
        if (op_len > 0) {
            tokens[token_count].type = matched_op_type; // Type from add_operator_dynamic
            tokens[token_count].text = storage_ptr;
            tokens[token_count].len = op_len;
            // ... (copy p to storage_ptr for op_len, check bounds, etc.) ...
            strncpy(storage_ptr, p, op_len);
            storage_ptr[op_len] = '\0';
            storage_ptr += (op_len + 1);
            remaining_storage -= (op_len + 1);

            p += op_len;
            token_count++;
            continue;
        }

        // 4. Attempt TOKEN_STRING
        if (*p == '"') {
            // ... (string tokenization logic similar to original bsh.c, consuming p)
            // Remember to handle escapes like \" and \\.
            // Example:
            const char *start_str = p;
            p++; // Skip opening quote
            while (*p && (*p != '"' || (*(p-1) == '\\' && (p-2 < start_str || *(p-2) != '\\')) ) ) { p++; }
            if (*p == '"') p++; // Skip closing quote
            
            tokens[token_count].type = TOKEN_STRING;
            tokens[token_count].text = storage_ptr;
            tokens[token_count].len = p - start_str;
            // ... (copy to storage_ptr, check bounds, etc.) ...
            token_count++;
            continue;
        }

        // 5. Attempt TOKEN_VARIABLE
        if (*p == '$') {
            // ... (variable tokenization logic similar to original bsh.c, consuming p) ...
            // Example:
            const char *start_var = p;
            p++; // Skip '$'
            if (*p == '{') { p++; while (*p && *p != '}') p++; if (*p == '}') p++; }
            else { while (isalnum((unsigned char)*p) || *p == '_') p++; }
            // Add array [index] parsing if needed, like in original bsh.c
            // if (*p == '[') { ... p++; while (*p && *p != ']') { if (*p == '$' ... ) { /* recurse or expand */ } p++; } if (*p == ']') p++; }


            tokens[token_count].type = TOKEN_VARIABLE;
            tokens[token_token_count].text = storage_ptr;
            tokens[token_count].len = p - start_var;
            // ... (copy to storage_ptr, check bounds, etc.) ...
            token_count++;
            continue;
        }

        // 6. Fallback to TOKEN_WORD
        const char *start_word = p_token_start; // Start from where this token attempt began
        while (*p && !isspace((unsigned char)*p)) {
            // Word terminates if any other known token type starts
            if (*p == '#' || *p == '"' || *p == '$' || match_operator_dynamic(p, NULL, NULL) > 0) {
                break;
            }
            // Define characters allowed in a word (e.g., alphanumeric + underscore)
            // If '-' is not part of numbers or operators, it could be here.
            // For this example, words are Alphanum and '_'.
            if (!isalnum((unsigned char)*p) && *p != '_') {
                break; // Not a valid word character
            }
            p++;
        }

        if (p > start_word) { // A word was formed
            tokens[token_count].type = TOKEN_WORD;
            tokens[token_count].text = storage_ptr;
            tokens[token_count].len = p - start_word;
            // ... (copy to storage_ptr, check bounds, etc.) ...
            strncpy(storage_ptr, start_word, tokens[token_count].len);
            storage_ptr[tokens[token_count].len] = '\0';
            storage_ptr += (tokens[token_count].len + 1);
            remaining_storage -= (tokens[token_count].len + 1);

            token_count++;
            continue;
        } else {
            // No token matched, and p didn't advance. This could be an error or an unsupported character.
            if (*p) { // Avoid infinite loop on unsupported char
                fprintf(stderr, "Tokenizer error: Unrecognized character or sequence starting with '%c'\n", *p);
                p++; // Skip it
            }
        }
    } // End while

    tokens[token_count].type = TOKEN_EOF; // Add EOF token
    tokens[token_count].text = "EOF";
    tokens[token_count].len = 3;
    // No storage needed for EOF text typically if it's a static string

    return token_count;
}

// --- Variable & Scope Management ---
// ... (rest of variable and scope management functions remain the same)
int enter_scope() {
    if (scope_stack_top + 1 >= MAX_SCOPE_DEPTH) {
        fprintf(stderr, "Error: Maximum scope depth exceeded (%d).\n", MAX_SCOPE_DEPTH);
        return -1; 
    }
    scope_stack_top++;
    scope_stack[scope_stack_top].scope_id = (scope_stack_top == 0 && next_scope_id == 1) ? GLOBAL_SCOPE_ID : next_scope_id++;
    if (scope_stack_top == 0) scope_stack[scope_stack_top].scope_id = GLOBAL_SCOPE_ID;

    return scope_stack[scope_stack_top].scope_id;
}

void leave_scope(int scope_id_to_leave) {
    if (scope_stack_top < 0 ) { 
        return;
    }
    if (scope_stack[scope_stack_top].scope_id != scope_id_to_leave) {
        if (scope_id_to_leave != GLOBAL_SCOPE_ID || scope_stack[scope_stack_top].scope_id != GLOBAL_SCOPE_ID) {
             fprintf(stderr, "Error: Scope mismatch on leave_scope. Trying to leave %d, current top is %d.\n",
                scope_id_to_leave, scope_stack[scope_stack_top].scope_id );
        }
        scope_stack_top--;
        return;
    }
    if (scope_id_to_leave != GLOBAL_SCOPE_ID) { 
        cleanup_variables_for_scope(scope_id_to_leave);
    }
    scope_stack_top--;
}

void cleanup_variables_for_scope(int scope_id) {
    if (scope_id == GLOBAL_SCOPE_ID) return; 

    Variable *current = variable_list_head;
    Variable *prev = NULL;
    while (current != NULL) {
        if (current->scope_id == scope_id) {
            Variable *to_delete = current;
            if (prev == NULL) { 
                variable_list_head = current->next;
            } else { 
                prev->next = current->next;
            }
            current = current->next; 
            if (to_delete->value) free(to_delete->value);
            free(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }
}

void free_all_variables() {
    Variable *current = variable_list_head;
    Variable *next_var;
    while (current != NULL) {
        next_var = current->next;
        if (current->value) free(current->value);
        free(current);
        current = next_var;
    }
    variable_list_head = NULL;
}

char* get_variable_scoped(const char *name_raw) {
    char clean_name[MAX_VAR_NAME_LEN];
    strncpy(clean_name, name_raw, MAX_VAR_NAME_LEN -1); clean_name[MAX_VAR_NAME_LEN-1] = '\0';
    trim_whitespace(clean_name);
    if (strlen(clean_name) == 0) return NULL;

    for (int i = scope_stack_top; i >= 0; i--) {
        int current_search_scope_id = scope_stack[i].scope_id;
        Variable *current_node = variable_list_head;
        while (current_node != NULL) {
            if (current_node->scope_id == current_search_scope_id && strcmp(current_node->name, clean_name) == 0) {
                return current_node->value; 
            }
            current_node = current_node->next;
        }
    }
    return NULL; 
}

void set_variable_scoped(const char *name_raw, const char *value_to_set, bool is_array_elem) {
    if (scope_stack_top < 0) {
        fprintf(stderr, "Critical Error: No active scope to set variable '%s'. Shell not initialized?\n", name_raw);
        return;
    }
    int current_scope_id = scope_stack[scope_stack_top].scope_id;

    char clean_name[MAX_VAR_NAME_LEN];
    strncpy(clean_name, name_raw, MAX_VAR_NAME_LEN -1); clean_name[MAX_VAR_NAME_LEN-1] = '\0';
    trim_whitespace(clean_name);
    if (strlen(clean_name) == 0) { fprintf(stderr, "Error: Cannot set variable with empty name.\n"); return; }

    Variable *current_node = variable_list_head;
    while (current_node != NULL) {
        if (current_node->scope_id == current_scope_id && strcmp(current_node->name, clean_name) == 0) {
            if (current_node->value) free(current_node->value); 
            current_node->value = strdup(value_to_set);
            if (!current_node->value) { perror("strdup failed for variable value update"); current_node->value = strdup("");  }
            current_node->is_array_element = is_array_elem;
            return;
        }
        current_node = current_node->next;
    }

    Variable *new_var = (Variable*)malloc(sizeof(Variable));
    if (!new_var) { perror("malloc for new variable failed"); return; }
    strncpy(new_var->name, clean_name, MAX_VAR_NAME_LEN - 1); new_var->name[MAX_VAR_NAME_LEN - 1] = '\0';
    new_var->value = strdup(value_to_set);
    if (!new_var->value) { perror("strdup failed for new variable value"); free(new_var); new_var = NULL;  return; }
    new_var->is_array_element = is_array_elem;
    new_var->scope_id = current_scope_id;
    new_var->next = variable_list_head; 
    variable_list_head = new_var;
}

void expand_variables_in_string_advanced(const char *input_str, char *expanded_str, size_t expanded_str_size) {
    const char *p_in = input_str;
    char *p_out = expanded_str;
    size_t remaining_size = expanded_str_size - 1; // For null terminator
    expanded_str[0] = '\0';

    while (*p_in && remaining_size > 0) {
        if (*p_in == '$') {
            p_in++; // Consume '$'
            
            char current_mangled_name[MAX_VAR_NAME_LEN * 4] = ""; // Buffer for var_prop1_prop2 etc. (increased size)
            char segment_buffer[MAX_VAR_NAME_LEN]; // For individual segment (base var or property name)
            char* pv = segment_buffer;
            bool first_segment = true;

            do { // Loop for base variable and subsequent dot-separated properties
                pv = segment_buffer; // Reset for current segment
                segment_buffer[0] = '\0';

                if (first_segment) { // Parsing the base variable name
                    if (*p_in == '{') {
                        p_in++; // Consume '{'
                        while (*p_in && *p_in != '}' && (pv - segment_buffer < MAX_VAR_NAME_LEN - 1)) {
                            *pv++ = *p_in++;
                        }
                        if (*p_in == '}') p_in++; // Consume '}'
                    } else {
                        while (isalnum((unsigned char)*p_in) || *p_in == '_') {
                            if (pv - segment_buffer < MAX_VAR_NAME_LEN - 1) *pv++ = *p_in++; else break;
                        }
                    }
                    *pv = '\0';
                    if (strlen(segment_buffer) == 0) { // Invalid: $ or ${}
                        // Output literal '$' and potentially braces if they were consumed
                        if (remaining_size > 0) { *p_out++ = '$'; remaining_size--; }
                        const char* temp_p = p_in -1; // Check what was before current p_in
                        if (*temp_p == '{' && remaining_size > 0) { *p_out++ = '{'; remaining_size--; }
                        if (*temp_p == '}' && remaining_size > 0) { *p_out++ = '}'; remaining_size--; }
                        goto next_char_in_input; // Break from $ processing, continue outer while
                    }
                    strncpy(current_mangled_name, segment_buffer, sizeof(current_mangled_name) - 1);
                    first_segment = false;
                } else { // Parsing a property name after a dot
                    if (*p_in == '$') { // Dynamic property: .$dynamicProp
                        p_in++; // Consume '$' for the dynamic part
                        char dynamic_prop_source_var_name[MAX_VAR_NAME_LEN];
                        char* pdv = dynamic_prop_source_var_name;
                        if (*p_in == '{') {
                            p_in++;
                            while (*p_in && *p_in != '}' && (pdv - dynamic_prop_source_var_name < MAX_VAR_NAME_LEN - 1)) {
                                *pdv++ = *p_in++;
                            }
                            if (*p_in == '}') p_in++;
                        } else {
                            while (isalnum((unsigned char)*p_in) || *p_in == '_') {
                                if (pdv - dynamic_prop_source_var_name < MAX_VAR_NAME_LEN - 1) *pdv++ = *p_in++; else break;
                            }
                        }
                        *pdv = '\0';
                        char* prop_name_from_var = get_variable_scoped(dynamic_prop_source_var_name);
                        if (prop_name_from_var) {
                            strncpy(segment_buffer, prop_name_from_var, MAX_VAR_NAME_LEN -1);
                            segment_buffer[MAX_VAR_NAME_LEN -1] = '\0';
                        } else {
                            segment_buffer[0] = '\0'; // Dynamic property name var not found
                        }
                    } else { // Literal property name: .prop
                        while (isalnum((unsigned char)*p_in) || *p_in == '_') { // Property names are like var names
                            if (pv - segment_buffer < MAX_VAR_NAME_LEN - 1) *pv++ = *p_in++; else break;
                        }
                        *pv = '\0';
                    }

                    if (strlen(segment_buffer) > 0) {
                        if (strlen(current_mangled_name) + 1 + strlen(segment_buffer) < sizeof(current_mangled_name)) {
                            strcat(current_mangled_name, "_");
                            strcat(current_mangled_name, segment_buffer);
                        } else {
                            segment_buffer[0] = '\0'; // Prevent overflow, effectively ending chain
                        }
                    } else {
                        // Invalid or empty property segment, chain broken.
                        // The value of current_mangled_name up to this point will be sought.
                        break; // Exit dot processing loop
                    }
                }
            } while (*p_in == '.'); // Check for next dot only if a valid segment was parsed
            // End of loop for base var and subsequent dot-separated properties

            char* value_to_insert = get_variable_scoped(current_mangled_name);
            if (value_to_insert) {
                size_t val_len = strlen(value_to_insert);
                if (val_len <= remaining_size) { // Check if it fits
                    strcpy(p_out, value_to_insert);
                    p_out += val_len;
                    remaining_size -= val_len;
                } else { // Not enough space
                    strncpy(p_out, value_to_insert, remaining_size);
                    p_out += remaining_size;
                    remaining_size = 0;
                }
            }
            // If value_to_insert is NULL, nothing is inserted for this $... sequence.

        } else if (*p_in == '\\' && *(p_in + 1) == '$') { // Escaped $
            p_in++; // Skip '\'
            if (remaining_size > 0) { *p_out++ = *p_in++; remaining_size--; }
        } else { // Regular character
            if (remaining_size > 0) { *p_out++ = *p_in++; remaining_size--; }
        }
        next_char_in_input:; // Label for goto
    }
    *p_out = '\0'; // Null-terminate the expanded string
}

char* get_array_element_scoped(const char* array_base_name, const char* index_str_raw_param) {
    char mangled_name[MAX_VAR_NAME_LEN * 2]; 
    snprintf(mangled_name, sizeof(mangled_name), "%s_ARRAYIDX_%s", array_base_name, index_str_raw_param);
    return get_variable_scoped(mangled_name);
}

void set_array_element_scoped(const char* array_base_name, const char* index_str_raw_param, const char* value) {
    char index_str_raw[INPUT_BUFFER_SIZE];
    strncpy(index_str_raw, index_str_raw_param, sizeof(index_str_raw) -1);
    index_str_raw[sizeof(index_str_raw)-1] = '\0';

    char expanded_index_val[INPUT_BUFFER_SIZE];
    if (index_str_raw[0] == '"' && index_str_raw[strlen(index_str_raw)-1] == '"') {
        char unescaped_idx[INPUT_BUFFER_SIZE];
        unescape_string(index_str_raw, unescaped_idx, sizeof(unescaped_idx));
        expand_variables_in_string_advanced(unescaped_idx, expanded_index_val, sizeof(expanded_index_val));
    } else if (index_str_raw[0] == '$') {
        expand_variables_in_string_advanced(index_str_raw, expanded_index_val, sizeof(expanded_index_val));
    } else { 
        strncpy(expanded_index_val, index_str_raw, sizeof(expanded_index_val)-1);
        expanded_index_val[sizeof(expanded_index_val)-1] = '\0';
    }
    char mangled_name[MAX_VAR_NAME_LEN * 2];
    snprintf(mangled_name, sizeof(mangled_name), "%s_ARRAYIDX_%s", array_base_name, expanded_index_val);
    set_variable_scoped(mangled_name, value, true); 
}


// --- Command Execution ---
// ... (find_command_in_path_dynamic, find_module_in_path, execute_external_command, execute_user_function remain the same)
bool find_command_in_path_dynamic(const char *command, char *full_path) {
    if (strchr(command, '/') != NULL) { 
        if (access(command, X_OK) == 0) {
            strncpy(full_path, command, MAX_FULL_PATH_LEN -1); full_path[MAX_FULL_PATH_LEN-1] = '\0';
            return true;
        }
        return false;
    }
    PathDirNode *current_path_node = path_list_head;
    while (current_path_node) {
        snprintf(full_path, MAX_FULL_PATH_LEN, "%s/%s", current_path_node->path, command);
        if (access(full_path, X_OK) == 0) return true;
        current_path_node = current_path_node->next;
    }
    return false;
}

bool find_module_in_path(const char* module_spec, char* result_full_path) {
    char module_path_part[MAX_FULL_PATH_LEN];
    strncpy(module_path_part, module_spec, sizeof(module_path_part) - 1);
    module_path_part[sizeof(module_path_part) - 1] = '\0';

    char *dot = strrchr(module_path_part, '.');
    if (dot && strchr(module_path_part, '/') == NULL) { 
        *dot = '/'; 
        strncat(module_path_part, ".bsh", sizeof(module_path_part) - strlen(module_path_part) - 1);
    } else if (strchr(module_path_part, '/') == NULL && (strstr(module_path_part, ".bsh") == NULL) ) {
        strncat(module_path_part, ".bsh", sizeof(module_path_part) - strlen(module_path_part) - 1);
    }

    char temp_path[PATH_MAX];
    if (realpath(module_path_part, temp_path) && access(temp_path, F_OK) == 0) {
        strncpy(result_full_path, temp_path, MAX_FULL_PATH_LEN -1);
        result_full_path[MAX_FULL_PATH_LEN-1] = '\0';
        return true;
    }
    if (access(module_path_part, F_OK) == 0) {
         strncpy(result_full_path, module_path_part, MAX_FULL_PATH_LEN -1); 
         result_full_path[MAX_FULL_PATH_LEN-1] = '\0';
         return true;
    }


    if (strchr(module_spec, '/') != NULL) { 
        return false;
    }

    PathDirNode *current_module_dir = module_path_list_head;
    while (current_module_dir) {
        snprintf(result_full_path, MAX_FULL_PATH_LEN, "%s/%s", current_module_dir->path, module_path_part);
        if (realpath(result_full_path, temp_path) && access(temp_path, F_OK) == 0) {
             strncpy(result_full_path, temp_path, MAX_FULL_PATH_LEN -1);
             result_full_path[MAX_FULL_PATH_LEN-1] = '\0';
            return true;
        } else if (access(result_full_path, F_OK) == 0) { 
            return true;
        }
        current_module_dir = current_module_dir->next;
    }
    result_full_path[0] = '\0'; 
    return false;
}

int execute_external_command(char *command_path, char **args, int arg_count, char *output_buffer, size_t output_buffer_size) {
    pid_t pid; int status; int pipefd[2] = {-1, -1};
    if (output_buffer) { if (pipe(pipefd) == -1) { perror("pipe failed for cmd output"); return -1; } }
    pid = fork();
    if (pid == 0) { 
        if (output_buffer) { close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); close(pipefd[1]); }
        execv(command_path, args);
        perror("execv failed"); exit(EXIT_FAILURE);
    } else if (pid < 0) { 
        perror("fork failed"); if (output_buffer) { close(pipefd[0]); close(pipefd[1]); } return -1;
    } else { 
        if (output_buffer) {
            close(pipefd[1]); ssize_t bytes_read; size_t total_bytes_read = 0;
            char read_buf[INPUT_BUFFER_SIZE]; output_buffer[0] = '\0';
            while((bytes_read = read(pipefd[0], read_buf, sizeof(read_buf)-1)) > 0) {
                if (total_bytes_read + bytes_read < output_buffer_size) {
                    read_buf[bytes_read] = '\0'; strcat(output_buffer, read_buf); total_bytes_read += bytes_read;
                } else { strncat(output_buffer, read_buf, output_buffer_size - total_bytes_read -1); break; }
            } close(pipefd[0]);
            char* nl = strrchr(output_buffer, '\n');
            while(nl && (nl == output_buffer + strlen(output_buffer) -1)) { *nl = '\0'; nl = strrchr(output_buffer, '\n');}
        }
        do { waitpid(pid, &status, WUNTRACED); } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        char status_str[12]; snprintf(status_str, sizeof(status_str), "%d", WEXITSTATUS(status));
        set_variable_scoped("LAST_COMMAND_STATUS", status_str, false);
        return WEXITSTATUS(status);
    }
    return -1; 
}

void execute_user_function(UserFunction* func, Token* call_arg_tokens, int call_arg_token_count, FILE* input_source_for_context) {
    if (!func) return;
    int function_scope_id = enter_scope();
    if (function_scope_id == -1) { return; }

    for (int i = 0; i < func->param_count; ++i) {
        if (i < call_arg_token_count) {
            char expanded_arg_val[INPUT_BUFFER_SIZE]; 
            if (call_arg_tokens[i].type == TOKEN_STRING) {
                 char unescaped_temp[INPUT_BUFFER_SIZE];
                 unescape_string(call_arg_tokens[i].text, unescaped_temp, sizeof(unescaped_temp));
                 expand_variables_in_string_advanced(unescaped_temp, expanded_arg_val, sizeof(expanded_arg_val));
            } else {
                 expand_variables_in_string_advanced(call_arg_tokens[i].text, expanded_arg_val, sizeof(expanded_arg_val));
            }
            set_variable_scoped(func->params[i], expanded_arg_val, false);
        } else {
            set_variable_scoped(func->params[i], "", false); 
        }
    }

    int func_outer_block_stack_top_bf = block_stack_top_bf;
    ExecutionState func_outer_exec_state = current_exec_state;
    current_exec_state = STATE_NORMAL; 

    for (int i = 0; i < func->line_count; ++i) {
        char line_copy[MAX_LINE_LENGTH]; 
        strncpy(line_copy, func->body[i], MAX_LINE_LENGTH-1); line_copy[MAX_LINE_LENGTH-1] = '\0';
        process_line(line_copy, NULL, i + 1, STATE_NORMAL); 
    }

    while(block_stack_top_bf > func_outer_block_stack_top_bf) {
        pop_block_bf();
    }
    current_exec_state = func_outer_exec_state;

    leave_scope(function_scope_id); 
}

// --- Built-in Commands & Operation Handlers ---
void handle_defkeyword_statement(Token *tokens, int num_tokens) {
    if (num_tokens != 3 || tokens[1].type != TOKEN_WORD || tokens[2].type != TOKEN_WORD) {
        fprintf(stderr, "Syntax: defkeyword <original_keyword> <new_alias>\n"); return;
    }
    if (current_exec_state == STATE_BLOCK_SKIP) return;
    add_keyword_alias(tokens[1].text, tokens[2].text);
}

bool is_comparison_or_assignment_operator(const char* op_str) {
    if (strcmp(op_str, "==") == 0 || strcmp(op_str, "!=") == 0 ||
        strcmp(op_str, ">") == 0  || strcmp(op_str, "<") == 0 ||
        strcmp(op_str, ">=") == 0 || strcmp(op_str, "<=") == 0 ||
        strcmp(op_str, "=") == 0) { 
        return true;
    }
    return false;
}

// For binary dynamic ops like $var = $op1 + $op2
bool invoke_bsh_function_for_op(const char* func_name_to_call,
                                const char* arg1_val, const char* arg2_val, const char* arg3_op, 
                                const char* bsh_result_var_name,
                                char* c_result_buffer, size_t c_result_buffer_size) {
    UserFunction* func = function_list;
    while (func) {
        if (strcmp(func->name, func_name_to_call) == 0) break;
        func = func->next;
    }
    if (!func) {
        fprintf(stderr, "Error: BSH internal handler function '%s' not found.\n", func_name_to_call);
        snprintf(c_result_buffer, c_result_buffer_size, "NO_HANDLER_ERROR");
        return false;
    }

    if (func->param_count != 4) { // Expects (op1_val_str, op_str, op2_val_str, result_holder_var_name)
                                  // or (op1_val_str, op2_val_str, op_str, result_holder_var_name)
                                  // The BSH script defines the order. The C call here assumes op1, op2, op_str for args.
         fprintf(stderr, "Error: BSH function '%s' has incorrect param count (expected 4, got %d) for binary op.\n", func_name_to_call, func->param_count);
         snprintf(c_result_buffer, c_result_buffer_size, "UNKNOWN_HANDLER_ERROR");
        return false;
    }

    Token call_tokens[4]; 
    char token_storage_arg1[INPUT_BUFFER_SIZE];
    char token_storage_arg2[INPUT_BUFFER_SIZE];
    char token_storage_arg3_op[MAX_OPERATOR_LEN + 1];
    char token_storage_arg4_res_var[MAX_VAR_NAME_LEN];

    // Argument 1 to BSH function (operand1 value)
    strncpy(token_storage_arg1, arg1_val, INPUT_BUFFER_SIZE -1); token_storage_arg1[INPUT_BUFFER_SIZE-1] = '\0';
    call_tokens[0].type = TOKEN_STRING; 
    call_tokens[0].text = token_storage_arg1;
    call_tokens[0].len = strlen(token_storage_arg1);

    // Argument 2 to BSH function (operand2 value)
    strncpy(token_storage_arg2, arg2_val, INPUT_BUFFER_SIZE -1); token_storage_arg2[INPUT_BUFFER_SIZE-1] = '\0';
    call_tokens[1].type = TOKEN_STRING;
    call_tokens[1].text = token_storage_arg2;
    call_tokens[1].len = strlen(token_storage_arg2);
    
    // Argument 3 to BSH function (operator string)
    strncpy(token_storage_arg3_op, arg3_op, MAX_OPERATOR_LEN); token_storage_arg3_op[MAX_OPERATOR_LEN] = '\0';
    call_tokens[2].type = TOKEN_STRING; 
    call_tokens[2].text = token_storage_arg3_op;
    call_tokens[2].len = strlen(token_storage_arg3_op);

    // Argument 4 to BSH function (result holder variable name)
    strncpy(token_storage_arg4_res_var, bsh_result_var_name, MAX_VAR_NAME_LEN -1); token_storage_arg4_res_var[MAX_VAR_NAME_LEN-1] = '\0';
    call_tokens[3].type = TOKEN_WORD; 
    call_tokens[3].text = token_storage_arg4_res_var;
    call_tokens[3].len = strlen(token_storage_arg4_res_var);

    execute_user_function(func, call_tokens, 4, NULL); 

    char* result_from_bsh = get_variable_scoped(bsh_result_var_name);
    if (result_from_bsh) {
        strncpy(c_result_buffer, result_from_bsh, c_result_buffer_size - 1);
        c_result_buffer[c_result_buffer_size - 1] = '\0';
    } else {
        snprintf(c_result_buffer, c_result_buffer_size, "OP_HANDLER_NO_RESULT_VAR<%s>", bsh_result_var_name);
        return false; 
    }
    return true;
}

// For unary ops like $var++ or ++$var
bool invoke_bsh_unary_op_call(const char* func_name_to_call,
                                const char* bsh_arg1_var_name_str,      // Name of the variable to be modified (e.g., "myvar")
                                const char* bsh_arg2_result_holder_var_name, // Name of BSH var to store result (e.g., "__TEMP_UNARY_OP_RES")
                                char* c_result_buffer, size_t c_result_buffer_size) {
    UserFunction* func = function_list;
    while (func) {
        if (strcmp(func->name, func_name_to_call) == 0) break;
        func = func->next;
    }
    if (!func) {
        fprintf(stderr, "Error: BSH internal unary handler function '%s' not found.\n", func_name_to_call);
        snprintf(c_result_buffer, c_result_buffer_size, "NO_UNARY_HANDLER_ERROR");
        return false;
    }

    if (func->param_count != 2) { // BSH function expects (var_name_string, result_holder_name_string)
        fprintf(stderr, "Error: BSH unary handler '%s' has incorrect param count (expected 2, got %d).\n", func_name_to_call, func->param_count);
        snprintf(c_result_buffer, c_result_buffer_size, "UNARY_HANDLER_PARAM_ERROR");
        return false;
    }

    Token call_tokens[2];
    char token_storage_arg1_var_name[MAX_VAR_NAME_LEN]; 
    char token_storage_arg2_res_holder_name[MAX_VAR_NAME_LEN];

    // Argument 1 to BSH function: the name of the variable to modify, passed as a string literal
    strncpy(token_storage_arg1_var_name, bsh_arg1_var_name_str, MAX_VAR_NAME_LEN -1); 
    token_storage_arg1_var_name[MAX_VAR_NAME_LEN-1] = '\0';
    call_tokens[0].type = TOKEN_STRING; 
    call_tokens[0].text = token_storage_arg1_var_name;
    call_tokens[0].len = strlen(token_storage_arg1_var_name);

    // Argument 2 to BSH function: the name of the variable where BSH func will store the "result of the expression"
    strncpy(token_storage_arg2_res_holder_name, bsh_arg2_result_holder_var_name, MAX_VAR_NAME_LEN -1);
    token_storage_arg2_res_holder_name[MAX_VAR_NAME_LEN-1] = '\0';
    call_tokens[1].type = TOKEN_WORD; // Pass this as a variable name (not its value)
    call_tokens[1].text = token_storage_arg2_res_holder_name;
    call_tokens[1].len = strlen(token_storage_arg2_res_holder_name);

    execute_user_function(func, call_tokens, 2, NULL);

    // Retrieve the result from the BSH result holder variable
    char* result_from_bsh = get_variable_scoped(bsh_arg2_result_holder_var_name);
    if (result_from_bsh) {
        strncpy(c_result_buffer, result_from_bsh, c_result_buffer_size - 1);
        c_result_buffer[c_result_buffer_size - 1] = '\0';
    } else {
        // This indicates the BSH handler didn't set the result variable.
        snprintf(c_result_buffer, c_result_buffer_size, "UNARY_OP_NO_RESULT_VAR<%s>", bsh_arg2_result_holder_var_name);
        // For inc/dec, we expect a result, so this is likely an issue in the BSH script.
        return false; 
    }
    return true;
}

void handle_defoperator_statement(Token *tokens, int num_tokens) {
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    if (num_tokens != 2 || tokens[1].type != TOKEN_STRING && tokens[1].type != TOKEN_WORD) {
        // Allow operator to be a quoted string or a simple word if it contains no special chars
        fprintf(stderr, "Syntax: defoperator <operator_symbol_string>\n");
        fprintf(stderr, "Example: defoperator \"++\"  OR  defoperator +\n");
        return;
    }

    char op_symbol[MAX_OPERATOR_LEN + 1];
    if (tokens[1].type == TOKEN_STRING) {
        // Unescape if it's a BSH string literal (e.g., defoperator "\"\\+\\-\"")
        // For simplicity, let's assume the text field of TOKEN_STRING for operators
        // would typically not contain complex escapes, or they are literal.
        // If "++", tokens[1].text is "\"++\"". We need "++".
        // The existing unescape_string might be too aggressive if not careful.
        // A simpler approach for operator symbols:
        size_t len = tokens[1].len;
        if (tokens[1].text[0] == '"' && tokens[1].text[len-1] == '"' && len >=2) {
            if (len - 2 > MAX_OPERATOR_LEN) {
                fprintf(stderr, "Error: Operator symbol in defoperator is too long: %s\n", tokens[1].text);
                return;
            }
            strncpy(op_symbol, tokens[1].text + 1, len - 2);
            op_symbol[len - 2] = '\0';
        } else { // Should not happen if TOKEN_STRING, but as a fallback for WORD
             if (len > MAX_OPERATOR_LEN) {
                fprintf(stderr, "Error: Operator symbol in defoperator is too long: %s\n", tokens[1].text);
                return;
            }
            strncpy(op_symbol, tokens[1].text, len);
            op_symbol[len] = '\0';
        }
    } else { // TOKEN_WORD
        if (tokens[1].len > MAX_OPERATOR_LEN) {
            fprintf(stderr, "Error: Operator symbol in defoperator is too long: %s\n", tokens[1].text);
            return;
        }
        strncpy(op_symbol, tokens[1].text, tokens[1].len);
        op_symbol[tokens[1].len] = '\0';
    }


    if (strlen(op_symbol) == 0) {
        fprintf(stderr, "Error: defoperator cannot define an empty operator symbol.\n");
        return;
    }

    // For now, all operators defined this way are generic TOKEN_OPERATOR.
    // If more specific token types were needed from scripts, the command would need another argument.
    add_operator_dynamic(op_symbol, TOKEN_OPERATOR);
    // printf("DEBUG: Defined operator '%s'\n", op_symbol); // Optional debug
}

bool evaluate_expression_tokens(Token* tokens_arr, int start_idx, int end_idx, char* result_buffer, size_t buffer_size) {
    result_buffer[0] = '\0';
    if (start_idx > end_idx) {
        // fprintf(stderr, "Debug: evaluate_expression_tokens called with empty token range.\n");
        return true; // Successfully evaluated to "empty"
    }

    // Find the main ternary operator symbols ('?' and ':') at the current nesting level.
    // This simplified version doesn't handle nested parentheses balancing for finding ? and :
    int qmark_idx = -1, colon_idx = -1;
    // int paren_level = 0; // Would be needed for robust ? : finding with parentheses

    for (int i = start_idx; i <= end_idx; ++i) {
        // if (tokens_arr[i].type == TOKEN_LPAREN) paren_level++;
        // else if (tokens_arr[i].type == TOKEN_RPAREN) paren_level--;
        // if (paren_level == 0) { // Only consider operators at the top level of parentheses
            if (tokens_arr[i].type == TOKEN_QMARK && qmark_idx == -1) { // First '?'
                qmark_idx = i;
            } else if (tokens_arr[i].type == TOKEN_COLON && qmark_idx != -1 && colon_idx == -1) { // First ':' after '?'
                colon_idx = i;
                // break; // Found a complete ? : structure at this level
            }
        // }
    }

    if (qmark_idx != -1 && colon_idx != -1 && qmark_idx < colon_idx) {
        // Ternary operator found: A ? B : C
        // A: tokens_arr[start_idx ... qmark_idx-1]
        // B: tokens_arr[qmark_idx+1 ... colon_idx-1]
        // C: tokens_arr[colon_idx+1 ... end_idx]

        char condition_result_str[INPUT_BUFFER_SIZE];
        if (!evaluate_expression_tokens(tokens_arr, start_idx, qmark_idx - 1, condition_result_str, sizeof(condition_result_str))) {
            strncpy(result_buffer, "TERNARY_COND_EVAL_ERROR", buffer_size - 1);
            return false; // Error evaluating condition
        }

        // Evaluate condition_result_str ("1", "true", "0", "false", or non-empty/empty for truthiness)
        bool condition_is_true = (strcmp(condition_result_str, "1") == 0 ||
                                  strcasecmp(condition_result_str, "true") == 0 ||
                                  (strlen(condition_result_str) > 0 && strcmp(condition_result_str,"0") != 0 && strcasecmp(condition_result_str,"false") !=0 ) );

        if (condition_is_true) {
            return evaluate_expression_tokens(tokens_arr, qmark_idx + 1, colon_idx - 1, result_buffer, buffer_size);
        } else {
            return evaluate_expression_tokens(tokens_arr, colon_idx + 1, end_idx, result_buffer, buffer_size);
        }
    } else {
        // No ternary operator at this level, or malformed. Evaluate as simple expression.
        // This part will handle: literal, variable, or single op1 OPR op2, or UNARY_OP op1 etc.
        int num_expr_tokens = (end_idx - start_idx) + 1;

        if (num_expr_tokens == 1) { // Single token: literal or variable
            Token* current_token = &tokens_arr[start_idx];
            if (current_token->type == TOKEN_STRING) {
                char unescaped[INPUT_BUFFER_SIZE];
                unescape_string(current_token->text, unescaped, sizeof(unescaped));
                expand_variables_in_string_advanced(unescaped, result_buffer, buffer_size);
            } else if (current_token->type == TOKEN_NUMBER || current_token->type == TOKEN_VARIABLE || current_token->type == TOKEN_WORD) {
                expand_variables_in_string_advanced(current_token->text, result_buffer, buffer_size);
            } else {
                fprintf(stderr, "Error: Cannot evaluate single token of type %d as expression.\n", current_token->type);
                strncpy(result_buffer, "EXPR_EVAL_ERROR", buffer_size -1); return false;
            }
            return true;
        } else if (num_expr_tokens == 2) { // Potential unary operation: OP $var or $var OP
            Token* op_token = NULL; Token* var_token = NULL; char context[10];
            if (tokens_arr[start_idx].type == TOKEN_OPERATOR && tokens_arr[start_idx+1].type == TOKEN_VARIABLE) { // ++$var
                op_token = &tokens_arr[start_idx]; var_token = &tokens_arr[start_idx+1]; strcpy(context, "prefix");
            } else if (tokens_arr[start_idx].type == TOKEN_VARIABLE && tokens_arr[start_idx+1].type == TOKEN_OPERATOR) { // $var++
                var_token = &tokens_arr[start_idx]; op_token = &tokens_arr[start_idx+1]; strcpy(context, "postfix");
            }

            if (op_token && var_token) {
                char var_name_clean[MAX_VAR_NAME_LEN]; // Extract from var_token->text (remove '$')
                // ... (logic to extract clean var name, same as in old handle_unary_op_statement / process_line for unary)
                 if (var_token->text[0] == '$') {
                    if (var_token->text[1] == '{') { /* ... */ } else { strncpy(var_name_clean, var_token->text + 1, MAX_VAR_NAME_LEN - 1); var_name_clean[MAX_VAR_NAME_LEN - 1] = '\0';}
                } else { /* error */ return false; }

                const char* temp_bsh_result_var = "__TEMP_EVAL_EXPR_RES";
                return invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                               var_name_clean, op_token->text, context,
                                               temp_bsh_result_var, result_buffer, buffer_size);
            } else {
                 fprintf(stderr, "Error: Malformed 2-token expression for evaluation.\n");
                 strncpy(result_buffer, "EXPR_EVAL_ERROR", buffer_size-1); return false;
            }
        } else if (num_expr_tokens == 3 && tokens_arr[start_idx+1].type == TOKEN_OPERATOR) { // op1 OPR op2
            char op1_expanded[INPUT_BUFFER_SIZE];
            char op2_expanded[INPUT_BUFFER_SIZE];

            // Expand operand1
            if (tokens_arr[start_idx].type == TOKEN_STRING) { /* unescape and expand */ unescape_string(tokens_arr[start_idx].text, op1_expanded, sizeof(op1_expanded)); expand_variables_in_string_advanced(op1_expanded, op1_expanded, sizeof(op1_expanded)); }
            else { expand_variables_in_string_advanced(tokens_arr[start_idx].text, op1_expanded, sizeof(op1_expanded)); }

            // Expand operand2
            if (tokens_arr[start_idx+2].type == TOKEN_STRING) { /* unescape and expand */ unescape_string(tokens_arr[start_idx+2].text, op2_expanded, sizeof(op2_expanded)); expand_variables_in_string_advanced(op2_expanded, op2_expanded, sizeof(op2_expanded));}
            else { expand_variables_in_string_advanced(tokens_arr[start_idx+2].text, op2_expanded, sizeof(op2_expanded)); }

            const char* operator_str = tokens_arr[start_idx+1].text;
            const char* temp_bsh_result_var = "__TEMP_EVAL_EXPR_RES";
            return invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                           op1_expanded, op2_expanded, operator_str,
                                           temp_bsh_result_var, result_buffer, buffer_size);
        } else {
            // More complex expression or unsupported: concatenate as string for now (basic fallback)
            // This path means the expression wasn't a single var/literal, recognized unary/binary, or ternary.
            // It will just combine the raw token texts or their variable expansions.
            result_buffer[0] = '\0'; size_t current_len = 0;
            for (int i = start_idx; i <= end_idx; ++i) {
                char expanded_part[INPUT_BUFFER_SIZE];
                if (tokens_arr[i].type == TOKEN_STRING) { /* unescape and expand */ unescape_string(tokens_arr[i].text, expanded_part, sizeof(expanded_part)); expand_variables_in_string_advanced(expanded_part, expanded_part, sizeof(expanded_part)); }
                else { expand_variables_in_string_advanced(tokens_arr[i].text, expanded_part, sizeof(expanded_part)); }
                
                if (current_len + strlen(expanded_part) + (i > start_idx ? 1 : 0) < buffer_size) {
                    if (i > start_idx) { strcat(result_buffer, " "); current_len++; }
                    strcat(result_buffer, expanded_part); current_len += strlen(expanded_part);
                } else { /* buffer full */ break; }
            }
            // This fallback concatenation might not be "evaluation" in a strict sense for complex expressions
            // but it's what bsh does for simple multi-token assignments if not an op or command.
            return true;
        }
    }
    return false; // Should not be reached if logic is complete
}

void handle_assignment_advanced(Token *tokens, int num_tokens) {
    if (num_tokens < 3 || tokens[0].type != TOKEN_VARIABLE || tokens[1].type != TOKEN_ASSIGN) {
        fprintf(stderr, "Assignment syntax: $variable = value | $array[index] = value\n"); return;
    }
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    char var_token_text_copy[MAX_VAR_NAME_LEN * 2]; 
    strncpy(var_token_text_copy, tokens[0].text + 1, sizeof(var_token_text_copy) -1); 
    var_token_text_copy[sizeof(var_token_text_copy)-1] = '\0';

    char base_var_name[MAX_VAR_NAME_LEN]; char index_str_raw[MAX_VAR_NAME_LEN] = ""; bool is_array_assignment = false;
    char* bracket_ptr = strchr(var_token_text_copy, '[');
    if (bracket_ptr) {
        char* end_bracket_ptr = strrchr(bracket_ptr, ']');
        if (end_bracket_ptr && end_bracket_ptr > bracket_ptr) {
            is_array_assignment = true;
            size_t base_len = bracket_ptr - var_token_text_copy;
            strncpy(base_var_name, var_token_text_copy, base_len); base_var_name[base_len] = '\0';
            size_t index_len = end_bracket_ptr - (bracket_ptr + 1);
            strncpy(index_str_raw, bracket_ptr + 1, index_len); index_str_raw[index_len] = '\0';
        } else { fprintf(stderr, "Malformed array assignment: %s\n", tokens[0].text); return; }
    } else { strncpy(base_var_name, var_token_text_copy, MAX_VAR_NAME_LEN - 1); base_var_name[MAX_VAR_NAME_LEN - 1] = '\0'; }

    char value_to_set[INPUT_BUFFER_SIZE]; value_to_set[0] = '\0'; 
    bool is_rhs_command = false;

    char rhs_eval_result[INPUT_BUFFER_SIZE]; // Buffer for the fully evaluated RHS
    rhs_eval_result[0] = '\0';
    bool eval_success = false;

    // Determine the start and end token indices for the RHS expression
    // RHS starts at tokens[2] (after $var and =)
    int rhs_start_idx = 2;
    int rhs_end_idx = num_tokens - 1;
    // Adjust rhs_end_idx if the last token is a comment
    if (rhs_end_idx >= rhs_start_idx && tokens[rhs_end_idx].type == TOKEN_COMMENT) {
        rhs_end_idx--;
    }

    if (rhs_start_idx <= rhs_end_idx) {
        // NEW: Use evaluate_expression_tokens for the entire RHS
        if (evaluate_expression_tokens(tokens, rhs_start_idx, rhs_end_idx, rhs_eval_result, sizeof(rhs_eval_result))) {
            // The object: prefix check for command substitution results would now need to be
            // handled *inside* evaluate_expression_tokens if a part of the expression
            // is a command substitution $(...).
            // For now, let's assume evaluate_expression_tokens handles its own $(...) expansion
            // and that result is what we have in rhs_eval_result.

            // Check if the result of evaluation (which might come from a cmd substitution within the expr)
            // has the "object:" prefix.
            bool structured_data_parsed = false;
            const char* data_to_parse_str = NULL;
            const char* detected_prefix_str = NULL; // e.g. "object:"

            if (strncmp(rhs_eval_result, "object:", strlen("object:")) == 0) { // As per user request
                data_to_parse_str = rhs_eval_result + strlen("object:");
                detected_prefix_str = "object:"; // For passing to the parser
                structured_data_parsed = true;
            }

            if (structured_data_parsed) {
                int current_scope = (scope_stack_top >= 0) ? scope_stack[scope_stack_top].scope_id : GLOBAL_SCOPE_ID;
                parse_and_flatten_bsh_object_string(data_to_parse_str, base_var_name, current_scope); // Needs scope_id if not using global
                
                // What to assign to the main variable? The raw data (minus prefix) or a marker.
                memmove(rhs_eval_result, (char*)data_to_parse_str, strlen(data_to_parse_str) + 1);
            }
            eval_success = true;
        } else {
            fprintf(stderr, "Error evaluating RHS for assignment to '%s'.\n", base_var_name);
            strncpy(rhs_eval_result, "RHS_EVAL_ERROR", sizeof(rhs_eval_result) - 1);
        }
    } else { // No tokens on RHS (e.g., $var =)
        eval_success = true; // Evaluates to empty string
    }

    // Final assignment
    if (eval_success) {
        if (is_array_assignment) {
            set_array_element_scoped(base_var_name, index_str_raw, rhs_eval_result);
        } else {
            set_variable_scoped(base_var_name, rhs_eval_result, false);
        }
    }
    // Note: The old logic for 'is_rhs_command' directly in handle_assignment_advanced is now
    // implicitly handled if evaluate_expression_tokens can evaluate command substitutions $(...).
    // If evaluate_expression_tokens cannot handle $(...) itself, then handle_assignment_advanced
    // would need to detect $(...) first, execute it, get its output, and *then* if that output
    // isn't an object prefix, it could be part of a larger expression to pass to evaluate_expression_tokens.
    // This simplified version assumes evaluate_expression_tokens can get the final string value.

    // --- END Prefix Check ---

    if (num_tokens > 2 && tokens[2].type == TOKEN_WORD) {
        char expanded_first_rhs_token[INPUT_BUFFER_SIZE];
        expand_variables_in_string_advanced(tokens[2].text, expanded_first_rhs_token, sizeof(expanded_first_rhs_token));
        
        UserFunction* func = function_list; 
        while(func) { 
            if (strcmp(expanded_first_rhs_token, func->name) == 0) { 
                is_rhs_command = true; 
                break; 
            } 
            func = func->next; 
        }
        if (!is_rhs_command) { 
            char full_cmd_path_check[MAX_FULL_PATH_LEN]; 
            if (find_command_in_path_dynamic(expanded_first_rhs_token, full_cmd_path_check)) {
                is_rhs_command = true; 
            }
        }
    }

    // Check for RHS like: $var = ++$op_var  (num_tokens = 4: $var = ++ $op_var)
    // or $var = $op_var++ (num_tokens = 4: $var = $op_var ++)
    bool rhs_is_unary_op = false;
    if (num_tokens == 4 && !is_rhs_command) { // $var = op $op_var OR $var = $op_var op
        if (tokens[2].type == TOKEN_OPERATOR && (strcmp(tokens[2].text,"++")==0 || strcmp(tokens[2].text,"--")==0) && tokens[3].type == TOKEN_VARIABLE) {
            // Prefix: $var = ++ $op_var
            rhs_is_unary_op = true;
            char op_var_name_clean[MAX_VAR_NAME_LEN];
            // Extract clean name from tokens[3].text (as in process_line)
            // ... (extraction logic for op_var_name_clean) ...
             if (tokens[3].text[0] == '$') { /* ... extract ... */ } else {strcpy(op_var_name_clean, "");}


            if (strlen(op_var_name_clean) > 0) {
                const char* temp_bsh_result_var = "__TEMP_ASSIGN_OP_RES";
                // Call BSH __dynamic_op_handler: (var_name, op_str, "prefix", result_holder)
                invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                               op_var_name_clean, tokens[2].text, "prefix",
                                               temp_bsh_result_var,
                                               value_to_set, sizeof(value_to_set));
            } else { /* error */ strncpy(value_to_set, "ASSIGN_OP_ERROR", sizeof(value_to_set)-1); }

        } else if (tokens[2].type == TOKEN_VARIABLE && tokens[3].type == TOKEN_OPERATOR && (strcmp(tokens[3].text,"++")==0 || strcmp(tokens[3].text,"--")==0)) {
            // Postfix: $var = $op_var ++
            rhs_is_unary_op = true;
            char op_var_name_clean[MAX_VAR_NAME_LEN];
            // Extract clean name from tokens[2].text (as in process_line)
            // ... (extraction logic for op_var_name_clean) ...
            if (tokens[2].text[0] == '$') { /* ... extract ... */ } else {strcpy(op_var_name_clean, "");}


            if (strlen(op_var_name_clean) > 0) {
                const char* temp_bsh_result_var = "__TEMP_ASSIGN_OP_RES";
                // Call BSH __dynamic_op_handler: (var_name, op_str, "postfix", result_holder)
                invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                               op_var_name_clean, tokens[3].text, "postfix",
                                               temp_bsh_result_var,
                                               value_to_set, sizeof(value_to_set));
            } else { /* error */ strncpy(value_to_set, "ASSIGN_OP_ERROR", sizeof(value_to_set)-1); }
        }
    }

   // Original binary expression assignment: $var = $op1 + $op2
    // (num_tokens >= 5 for $var = op1 op op2)
    if (!rhs_is_unary_op && num_tokens >= 5 && !is_rhs_command &&
        /* ... original conditions for binary op ... */
        tokens[3].type == TOKEN_OPERATOR && !is_comparison_or_assignment_operator(tokens[3].text) 
        /* ... */
        ) {
        if (current_exec_state == STATE_BLOCK_SKIP) return;

        char op1_expanded[INPUT_BUFFER_SIZE];
        char op2_expanded[INPUT_BUFFER_SIZE];
        // char temp_result_val_c[INPUT_BUFFER_SIZE]; // Now value_to_set directly
        const char* operator_str = tokens[3].text;
        const char* temp_bsh_result_var = "__TEMP_ASSIGN_OP_RES";

        // ... (expansion of op1_expanded, op2_expanded as before)
        expand_variables_in_string_advanced(tokens[2].text, op1_expanded, sizeof(op1_expanded));
        expand_variables_in_string_advanced(tokens[4].text, op2_expanded, sizeof(op2_expanded));


        // Call BSH __dynamic_op_handler: (val1, val2, op_str, result_holder)
        if (invoke_bsh_dynamic_op_handler("__dynamic_op_handler",
                                    op1_expanded, op2_expanded, operator_str,
                                    temp_bsh_result_var,
                                    value_to_set, sizeof(value_to_set))) {
            // value_to_set is now populated
        } else {
            fprintf(stderr, "Error executing dynamic binary operation for assignment RHS.\n");
            strncpy(value_to_set, "ASSIGN_OP_ERROR", sizeof(value_to_set)-1);
        }
    }

     if (!rhs_is_unary_op && is_rhs_command) {
        // ... (original cmd_args construction for execute_external_command) ...
        char *cmd_args[MAX_ARGS + 1];
        char expanded_cmd_args_storage[MAX_ARGS][INPUT_BUFFER_SIZE];
        int cmd_arg_count = 0;
        // (Loop to populate cmd_args and expanded_cmd_args_storage from tokens[2] onwards)
        for (int i = 2; i < num_tokens; i++) {
            if (tokens[i].type == TOKEN_COMMENT) break;
            if (cmd_arg_count < MAX_ARGS) {
                 if (tokens[i].type == TOKEN_STRING) {
                    char unescaped_val[INPUT_BUFFER_SIZE];
                    unescape_string(tokens[i].text, unescaped_val, sizeof(unescaped_val));
                    expand_variables_in_string_advanced(unescaped_val, expanded_cmd_args_storage[cmd_arg_count], INPUT_BUFFER_SIZE);
                } else {
                    expand_variables_in_string_advanced(tokens[i].text, expanded_cmd_args_storage[cmd_arg_count], INPUT_BUFFER_SIZE);
                }
                cmd_args[cmd_arg_count] = expanded_cmd_args_storage[cmd_arg_count];
                cmd_arg_count++;
            } else { /* ... too many args warning ... */ break; }
        }
        cmd_args[cmd_arg_count] = NULL;

        if (cmd_arg_count > 0) {
            // ... (check if it's a user function, which isn't captured this way) ...
            bool is_user_func_rhs_final_check = false;
            UserFunction* user_func_check = function_list;
            while(user_func_check){ if(strcmp(cmd_args[0], user_func_check->name) == 0) { is_user_func_rhs_final_check = true; break; } user_func_check = user_func_check->next; }

            if(is_user_func_rhs_final_check){
                fprintf(stderr, "Assigning output of user-defined functions to variables is not directly supported for capture. Execute separately and use a result variable set by the function.\n");
                strncpy(value_to_set, "USER_FUNC_ASSIGN_UNSUPPORTED", sizeof(value_to_set) -1);
            } else {
                char full_cmd_path_for_exec[MAX_FULL_PATH_LEN];
                if (find_command_in_path_dynamic(cmd_args[0], full_cmd_path_for_exec)) {
                    // value_to_set is the output_buffer for execute_external_command
                    execute_external_command(full_cmd_path_for_exec, cmd_args, cmd_arg_count, value_to_set, sizeof(value_to_set));
                    
                    // --- NEW: Check for structured data prefix ---
                    bool structured_data_parsed = false;
                    const char* data_to_parse = NULL;
                    const char* detected_prefix_str = NULL;

                    if (strncmp(value_to_set, JSON_STDOUT_PREFIX, strlen(JSON_STDOUT_PREFIX)) == 0) {
                        data_to_parse = value_to_set + strlen(JSON_STDOUT_PREFIX);
                        detected_prefix_str = JSON_STDOUT_PREFIX;
                        structured_data_parsed = true;
                    } else if (strncmp(value_to_set, OBJECT_STDOUT_PREFIX, strlen(OBJECT_STDOUT_PREFIX)) == 0) {
                        data_to_parse = value_to_set + strlen(OBJECT_STDOUT_PREFIX);
                        detected_prefix_str = OBJECT_STDOUT_PREFIX;
                        structured_data_parsed = true;
                    }

                    if (structured_data_parsed) {
                        // Call the C function to parse and flatten the structured data.
                        // It will create BSH variables like $base_var_name_key = value
                        int current_scope = (scope_stack_top >= 0) ? scope_stack[scope_stack_top].scope_id : GLOBAL_SCOPE_ID;
                        parse_and_flatten_bsh_object_string(data_to_parse, base_var_name, detected_prefix_str, current_scope);

                        // What should the main variable ($base_var_name) be set to?
                        // Option 1: The raw string data (without prefix)
                        // Option 2: A special marker value
                        // Let's go with Option 1 for now.
                        memmove(value_to_set, (char*)data_to_parse, strlen(data_to_parse) + 1); // Shift data to overwrite prefix
                    }
                    // If not structured_data_parsed, value_to_set contains the raw command output as before.
                    // --- END NEW ---

                } else {
                    fprintf(stderr, "Command for assignment not found: %s\n", cmd_args[0]);
                    strncpy(value_to_set, "CMD_NOT_FOUND_ERROR", sizeof(value_to_set)-1);
                }
            }
        } else { 
            /* ... internal error ... */ 
            printf("INTERNAL DEV ERROR: check handl_assigment_advanced in bsh.c 495823")
        }
        } else if (!rhs_is_unary_op) { // This is the original path for simple value concatenation for assignment
            // (Concatenate tokens from index 2 onwards into value_to_set, expanding variables)
            char combined_value[INPUT_BUFFER_SIZE] = ""; size_t current_len = 0;
            for (int i = 2; i < num_tokens; i++) {
                if (tokens[i].type == TOKEN_COMMENT) break;
                char expanded_token_val[INPUT_BUFFER_SIZE];
                if (tokens[i].type == TOKEN_STRING) {
                    char unescaped_temp[INPUT_BUFFER_SIZE];
                    unescape_string(tokens[i].text, unescaped_temp, sizeof(unescaped_temp));
                    expand_variables_in_string_advanced(unescaped_temp, expanded_token_val, sizeof(expanded_token_val));
                } else {
                    expand_variables_in_string_advanced(tokens[i].text, expanded_token_val, sizeof(expanded_token_val));
                }
                size_t token_len = strlen(expanded_token_val);
                if (current_len + token_len + (current_len > 0 ? 1 : 0) < INPUT_BUFFER_SIZE) { // Add space if not first part
                    if (current_len > 0) { strcat(combined_value, " "); current_len++; }
                    strcat(combined_value, expanded_token_val); current_len += token_len;
                } else { /* value too long */ break; }
            }
            strncpy(value_to_set, combined_value, sizeof(value_to_set) -1);
            value_to_set[sizeof(value_to_set)-1] = '\0';
        }
        // (The rest of the assignment logic with value_to_set remains)

        // Final assignment to LHS variable
        if (is_array_assignment) {
            set_array_element_scoped(base_var_name, index_str_raw, value_to_set);
        } else {
            set_variable_scoped(base_var_name, value_to_set, false);
        }
    }

    // Finally, assign value_to_set to the LHS variable
    if (is_array_assignment) set_array_element_scoped(base_var_name, index_str_raw, value_to_set);
    else set_variable_scoped(base_var_name, value_to_set, false);
}

bool evaluate_condition_advanced(Token* operand1_token, Token* operator_token, Token* operand2_token) {
    // ... (remains the same)
    if (!operand1_token || !operator_token || !operand2_token) return false;

    char val1_expanded[INPUT_BUFFER_SIZE], val2_expanded[INPUT_BUFFER_SIZE];
    if (operand1_token->type == TOKEN_STRING) { char unescaped[INPUT_BUFFER_SIZE]; unescape_string(operand1_token->text, unescaped, sizeof(unescaped)); expand_variables_in_string_advanced(unescaped, val1_expanded, sizeof(val1_expanded));
    } else { expand_variables_in_string_advanced(operand1_token->text, val1_expanded, sizeof(val1_expanded)); }
    if (operand2_token->type == TOKEN_STRING) { char unescaped[INPUT_BUFFER_SIZE]; unescape_string(operand2_token->text, unescaped, sizeof(unescaped)); expand_variables_in_string_advanced(unescaped, val2_expanded, sizeof(val2_expanded));
    } else { expand_variables_in_string_advanced(operand2_token->text, val2_expanded, sizeof(val2_expanded)); }

    const char* op_str = operator_token->text;
    if (strcmp(op_str, "==") == 0) return strcmp(val1_expanded, val2_expanded) == 0;
    if (strcmp(op_str, "!=") == 0) return strcmp(val1_expanded, val2_expanded) != 0;

    long num1, num2; char *endptr1, *endptr2;
    errno = 0; num1 = strtol(val1_expanded, &endptr1, 10); bool num1_valid = (errno == 0 && val1_expanded[0] != '\0' && *endptr1 == '\0');
    errno = 0; num2 = strtol(val2_expanded, &endptr2, 10); bool num2_valid = (errno == 0 && val2_expanded[0] != '\0' && *endptr2 == '\0');
    bool numeric_possible = num1_valid && num2_valid;

    if (numeric_possible) {
        if (strcmp(op_str, ">") == 0) return num1 > num2; if (strcmp(op_str, "<") == 0) return num1 < num2;
        if (strcmp(op_str, ">=") == 0) return num1 >= num2; if (strcmp(op_str, "<=") == 0) return num1 <= num2;
    } else { 
        if (strcmp(op_str, ">") == 0) return strcmp(val1_expanded, val2_expanded) > 0;
        if (strcmp(op_str, "<") == 0) return strcmp(val1_expanded, val2_expanded) < 0;
        if (strcmp(op_str, ">=") == 0) return strcmp(val1_expanded, val2_expanded) >= 0;
        if (strcmp(op_str, "<=") == 0) return strcmp(val1_expanded, val2_expanded) <= 0;
    }
    fprintf(stderr, "Unsupported operator or type mismatch in condition: '%s' %s '%s'\n", val1_expanded, op_str, val2_expanded);
    return false;
}

void handle_if_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no) {
    // ... (remains the same)
    if (num_tokens < 2) { 
        fprintf(stderr, "Syntax error for 'if'. Expected: if [!] <condition_value_or_variable> [{]\n");
        if (block_stack_top_bf < MAX_NESTING_DEPTH -1 && current_exec_state != STATE_BLOCK_SKIP) {
           push_block_bf(BLOCK_TYPE_IF, false, 0, current_line_no); current_exec_state = STATE_BLOCK_SKIP;
        } return;
    }

    bool condition_result = false;
    bool negate_result = false;
    int condition_token_idx = 1;

    if (current_exec_state != STATE_BLOCK_SKIP) {
        if (tokens[1].type == TOKEN_OPERATOR && strcmp(tokens[1].text, "!") == 0) {
            if (num_tokens < 3) { 
                fprintf(stderr, "Syntax error for 'if !'. Expected: if ! <condition_value_or_variable> [{]\n");
                if (block_stack_top_bf < MAX_NESTING_DEPTH -1) { 
                    push_block_bf(BLOCK_TYPE_IF, false, 0, current_line_no); current_exec_state = STATE_BLOCK_SKIP;
                }
                return;
            }
            negate_result = true;
            condition_token_idx = 2;
        }

        if (num_tokens >= condition_token_idx + 3 && tokens[condition_token_idx + 1].type == TOKEN_OPERATOR) {
            condition_result = evaluate_condition_advanced(&tokens[condition_token_idx], &tokens[condition_token_idx+1], &tokens[condition_token_idx+2]);
        } else { 
            char condition_value_expanded[INPUT_BUFFER_SIZE];
            if (tokens[condition_token_idx].type == TOKEN_STRING) {
                char unescaped[INPUT_BUFFER_SIZE];
                unescape_string(tokens[condition_token_idx].text, unescaped, sizeof(unescaped));
                expand_variables_in_string_advanced(unescaped, condition_value_expanded, sizeof(condition_value_expanded));
            } else {
                expand_variables_in_string_advanced(tokens[condition_token_idx].text, condition_value_expanded, sizeof(condition_value_expanded));
            }
            condition_result = (strcmp(condition_value_expanded, "1") == 0 || 
                                (strcmp(condition_value_expanded, "true") == 0) ||
                                (strlen(condition_value_expanded) > 0 && strcmp(condition_value_expanded,"0") != 0 && strcmp(condition_value_expanded,"false") !=0 ) );
        }


        if (negate_result) {
            condition_result = !condition_result;
        }
    } 

    push_block_bf(BLOCK_TYPE_IF, condition_result, 0, current_line_no);
    if (condition_result && current_exec_state != STATE_BLOCK_SKIP) { 
        current_exec_state = STATE_BLOCK_EXECUTE;
    } else {
        current_exec_state = STATE_BLOCK_SKIP;
    }

    int brace_expected_after_idx = condition_token_idx;
    if (num_tokens >= condition_token_idx + 3 && tokens[condition_token_idx + 1].type == TOKEN_OPERATOR) {
        brace_expected_after_idx = condition_token_idx + 2; 
    }
    int last_substantive_token_idx_before_brace_or_comment = brace_expected_after_idx;

    if (num_tokens > last_substantive_token_idx_before_brace_or_comment + 1 && tokens[last_substantive_token_idx_before_brace_or_comment+1].type == TOKEN_LBRACE) {
    } else if (num_tokens == last_substantive_token_idx_before_brace_or_comment + 1) {
    } else if (num_tokens > last_substantive_token_idx_before_brace_or_comment + 1 && tokens[num_tokens-1].type == TOKEN_LBRACE) {
    } else if (tokens[num_tokens-1].type == TOKEN_COMMENT && num_tokens-1 == last_substantive_token_idx_before_brace_or_comment +1){
    }
    else if (num_tokens > last_substantive_token_idx_before_brace_or_comment + 1) { 
         fprintf(stderr, "Syntax error for 'if': Unexpected tokens after condition/expression. '{' expected or end of line.\n");
    }
}

void handle_while_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no) {
    // ... (remains the same)
    if (num_tokens < 2) {
        fprintf(stderr, "Syntax error for 'while'. Expected: while [!] <condition_value_or_variable_or_expr> [{]\n");
        if (block_stack_top_bf < MAX_NESTING_DEPTH -1 && current_exec_state != STATE_BLOCK_SKIP) {
           push_block_bf(BLOCK_TYPE_WHILE, false, get_file_pos(input_source), current_line_no); current_exec_state = STATE_BLOCK_SKIP;
        } return;
    }

    bool condition_result = false;
    bool negate_result = false;
    int condition_token_idx = 1;
    long loop_fpos_at_while_line = get_file_pos(input_source); 
    
    if (current_exec_state != STATE_BLOCK_SKIP) {
        if (tokens[1].type == TOKEN_OPERATOR && strcmp(tokens[1].text, "!") == 0) {
            if (num_tokens < 3) {
                fprintf(stderr, "Syntax error for 'while !'. Expected: while ! <condition_value_or_variable_or_expr> [{]\n");
                 if (block_stack_top_bf < MAX_NESTING_DEPTH -1) {
                    push_block_bf(BLOCK_TYPE_WHILE, false, loop_fpos_at_while_line, current_line_no); current_exec_state = STATE_BLOCK_SKIP;
                }
                return;
            }
            negate_result = true;
            condition_token_idx = 2;
        }

        if (num_tokens >= condition_token_idx + 3 && tokens[condition_token_idx + 1].type == TOKEN_OPERATOR) {
             condition_result = evaluate_condition_advanced(&tokens[condition_token_idx], &tokens[condition_token_idx+1], &tokens[condition_token_idx+2]);
        } else { 
            char condition_value_expanded[INPUT_BUFFER_SIZE];
            if (tokens[condition_token_idx].type == TOKEN_STRING) {
                char unescaped[INPUT_BUFFER_SIZE];
                unescape_string(tokens[condition_token_idx].text, unescaped, sizeof(unescaped));
                expand_variables_in_string_advanced(unescaped, condition_value_expanded, sizeof(condition_value_expanded));
            } else {
                expand_variables_in_string_advanced(tokens[condition_token_idx].text, condition_value_expanded, sizeof(condition_value_expanded));
            }
            condition_result = (strcmp(condition_value_expanded, "1") == 0 || 
                                (strcmp(condition_value_expanded, "true") == 0) ||
                                (strlen(condition_value_expanded) > 0 && strcmp(condition_value_expanded,"0") != 0 && strcmp(condition_value_expanded,"false") !=0 ) );
        }

        if (negate_result) {
            condition_result = !condition_result;
        }
    }

    push_block_bf(BLOCK_TYPE_WHILE, condition_result, loop_fpos_at_while_line, current_line_no);
    if (condition_result && current_exec_state != STATE_BLOCK_SKIP) {
        current_exec_state = STATE_BLOCK_EXECUTE;
    } else {
        current_exec_state = STATE_BLOCK_SKIP;
    }
    
    int brace_expected_after_idx = condition_token_idx;
    if (num_tokens >= condition_token_idx + 3 && tokens[condition_token_idx + 1].type == TOKEN_OPERATOR) {
        brace_expected_after_idx = condition_token_idx + 2; 
    }

    int last_substantive_token_idx_before_brace_or_comment = brace_expected_after_idx;

    if (num_tokens > last_substantive_token_idx_before_brace_or_comment + 1 && tokens[last_substantive_token_idx_before_brace_or_comment+1].type == TOKEN_LBRACE) {}
    else if (num_tokens == last_substantive_token_idx_before_brace_or_comment + 1) {}
    else if (tokens[num_tokens-1].type == TOKEN_COMMENT && num_tokens-1 == last_substantive_token_idx_before_brace_or_comment +1){}
    else if (num_tokens > last_substantive_token_idx_before_brace_or_comment + 1) {
         fprintf(stderr, "Syntax error for 'while': Unexpected tokens after condition/expression. '{' expected or end of line.\n");
    }
}

void handle_else_statement_advanced(Token *tokens, int num_tokens, FILE* input_source, int current_line_no) {
    // ... (remains the same)
    BlockFrame* prev_block_frame = peek_block_bf();
    if (!prev_block_frame || (prev_block_frame->type != BLOCK_TYPE_IF && prev_block_frame->type != BLOCK_TYPE_ELSE)) {
        fprintf(stderr, "Error: 'else' without a preceding 'if' or 'else if' block on line %d.\n", current_line_no);
        if (current_exec_state != STATE_BLOCK_SKIP) { 
            current_exec_state = STATE_BLOCK_SKIP; 
        } return;
    }

    BlockFrame closed_if_or_else_if = *pop_block_bf(); 
    bool execute_this_else_branch = false;

    if (closed_if_or_else_if.condition_true) { 
        execute_this_else_branch = false;
    } else { 
        if (num_tokens > 1 && tokens[1].type == TOKEN_WORD && strcmp(resolve_keyword_alias(tokens[1].text), "if") == 0) { 
            int condition_token_idx = 2; 
            bool negate_result = false;

            if (num_tokens < 3) { 
                fprintf(stderr, "Syntax error for 'else if'. Expected: else if [!] <condition_value_or_variable_or_expr> [{]\n");
                execute_this_else_branch = false; 
            } else {
                if (tokens[2].type == TOKEN_OPERATOR && strcmp(tokens[2].text, "!") == 0) { 
                    if (num_tokens < 4) { 
                        fprintf(stderr, "Syntax error for 'else if !'. Expected: else if ! <condition_value_or_variable_or_expr> [{]\n");
                        execute_this_else_branch = false;
                    } else {
                        negate_result = true;
                        condition_token_idx = 3; 
                    }
                }
                if (execute_this_else_branch == false && !(negate_result && num_tokens <4) && !(num_tokens <3) ) { 
                    if (current_exec_state != STATE_BLOCK_SKIP) { 
                        if (num_tokens >= condition_token_idx + 3 && tokens[condition_token_idx + 1].type == TOKEN_OPERATOR) {
                             execute_this_else_branch = evaluate_condition_advanced(&tokens[condition_token_idx], &tokens[condition_token_idx+1], &tokens[condition_token_idx+2]);
                        } else { 
                            char condition_value_expanded[INPUT_BUFFER_SIZE];
                            if (tokens[condition_token_idx].type == TOKEN_STRING) {
                                char unescaped[INPUT_BUFFER_SIZE];
                                unescape_string(tokens[condition_token_idx].text, unescaped, sizeof(unescaped));
                                expand_variables_in_string_advanced(unescaped, condition_value_expanded, sizeof(condition_value_expanded));
                            } else {
                                expand_variables_in_string_advanced(tokens[condition_token_idx].text, condition_value_expanded, sizeof(condition_value_expanded));
                            }
                             execute_this_else_branch = (strcmp(condition_value_expanded, "1") == 0 || 
                                (strcmp(condition_value_expanded, "true") == 0) ||
                                (strlen(condition_value_expanded) > 0 && strcmp(condition_value_expanded,"0") != 0 && strcmp(condition_value_expanded,"false") !=0 ) );
                        }
                        if (negate_result) execute_this_else_branch = !execute_this_else_branch;
                    } else { 
                        execute_this_else_branch = false;
                    }
                }
            }
        } else { 
            execute_this_else_branch = true; 
        }
    }

    push_block_bf(BLOCK_TYPE_ELSE, execute_this_else_branch, 0, current_line_no);
    if (execute_this_else_branch && current_exec_state != STATE_BLOCK_SKIP) { 
        current_exec_state = STATE_BLOCK_EXECUTE;
    } else {
        current_exec_state = STATE_BLOCK_SKIP;
    }

    int base_token_count_for_brace_check = 1; 
    if (num_tokens > 1 && tokens[1].type == TOKEN_WORD && strcmp(resolve_keyword_alias(tokens[1].text), "if") == 0) { 
        base_token_count_for_brace_check = 2; 
        if (num_tokens > 2 && tokens[2].type == TOKEN_OPERATOR && strcmp(tokens[2].text, "!") == 0) { 
             base_token_count_for_brace_check = 3; 
        }
        if (num_tokens >= base_token_count_for_brace_check + 3 && tokens[base_token_count_for_brace_check + 1].type == TOKEN_OPERATOR) {
            base_token_count_for_brace_check += 2; 
        }
        base_token_count_for_brace_check++; 
    }

    if (num_tokens > base_token_count_for_brace_check && tokens[base_token_count_for_brace_check].type == TOKEN_LBRACE) {  }
    else if (num_tokens == base_token_count_for_brace_check) {  }
    else if (num_tokens > base_token_count_for_brace_check && tokens[num_tokens-1].type == TOKEN_LBRACE) {  }
    else if (tokens[num_tokens-1].type == TOKEN_COMMENT && num_tokens-1 == base_token_count_for_brace_check){}
    else if (num_tokens > base_token_count_for_brace_check && tokens[base_token_count_for_brace_check].type != TOKEN_COMMENT) { 
        fprintf(stderr, "Syntax error for 'else'/'else if' on line %d: Unexpected tokens after condition/expression. '{' expected or end of line.\n", current_line_no);
    }
}

void handle_defunc_statement_advanced(Token *tokens, int num_tokens) {
    // ... (remains the same)
    if (num_tokens < 2 || tokens[1].type != TOKEN_WORD) {
        fprintf(stderr, "Syntax: defunc <funcname> [(param1 ...)] [{]\n"); return;
    }
    if (is_defining_function && current_exec_state != STATE_IMPORT_PARSING) {
        fprintf(stderr, "Error: Cannot nest function definitions during normal execution.\n"); return;
    }
    if (current_exec_state == STATE_BLOCK_SKIP && current_exec_state != STATE_IMPORT_PARSING) {
        push_block_bf(BLOCK_TYPE_FUNCTION_DEF, false, 0, 0); return; 
    }

    current_function_definition = (UserFunction*)malloc(sizeof(UserFunction));
    if (!current_function_definition) { perror("malloc for function definition failed"); return; }
    memset(current_function_definition, 0, sizeof(UserFunction));
    strncpy(current_function_definition->name, tokens[1].text, MAX_VAR_NAME_LEN - 1);

    int token_idx = 2;
    if (token_idx < num_tokens && tokens[token_idx].type == TOKEN_LPAREN) {
        token_idx++; 
        while(token_idx < num_tokens && tokens[token_idx].type != TOKEN_RPAREN) {
            if (tokens[token_idx].type == TOKEN_WORD) {
                if (current_function_definition->param_count < MAX_FUNC_PARAMS) {
                    strncpy(current_function_definition->params[current_function_definition->param_count++], tokens[token_idx].text, MAX_VAR_NAME_LEN -1);
                } else { fprintf(stderr, "Too many parameters for function %s.\n", current_function_definition->name); free(current_function_definition); current_function_definition = NULL; return; }
            } else if (tokens[token_idx].type == TOKEN_COMMENT) { 
                break; 
            }else { fprintf(stderr, "Syntax error in function parameters: Expected word for %s, got '%s'.\n", current_function_definition->name, tokens[token_idx].text); free(current_function_definition); current_function_definition = NULL; return; }
            token_idx++;
        }
        if (token_idx < num_tokens && tokens[token_idx].type == TOKEN_RPAREN) token_idx++; 
        else if (!(token_idx < num_tokens && tokens[token_idx].type == TOKEN_COMMENT)) { 
             fprintf(stderr, "Syntax error in function parameters: missing ')' for %s.\n", current_function_definition->name); free(current_function_definition); current_function_definition = NULL; return; 
        }
    }
    while(token_idx < num_tokens && tokens[token_idx].type == TOKEN_COMMENT) {
        token_idx++;
    }

    if (token_idx < num_tokens && tokens[token_idx].type == TOKEN_LBRACE) { 
        is_defining_function = true;
        if (current_exec_state != STATE_IMPORT_PARSING) current_exec_state = STATE_DEFINE_FUNC_BODY;
        push_block_bf(BLOCK_TYPE_FUNCTION_DEF, true, 0, 0); 
    } else if (token_idx == num_tokens) { 
        is_defining_function = true;
        if (current_exec_state != STATE_IMPORT_PARSING) current_exec_state = STATE_DEFINE_FUNC_BODY;
    } else {
        fprintf(stderr, "Syntax error in function definition: '{' expected for %s, got '%s'.\n", current_function_definition->name, tokens[token_idx].text);
        free(current_function_definition); current_function_definition = NULL;
    }
}

void handle_inc_dec_statement_advanced(Token *tokens, int num_tokens, bool increment) {
    // ... (remains the same, this is for 'inc'/'dec' keywords)
    if (num_tokens != 2 || (tokens[1].type != TOKEN_VARIABLE && tokens[1].type != TOKEN_WORD)) {
        fprintf(stderr, "Syntax: %s <$varname_or_varname | $arr[idx]>\n", increment ? "inc" : "dec"); return;
    }
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    const char* var_name_token_text = tokens[1].text;
    char var_name_or_base[MAX_VAR_NAME_LEN];
    bool is_array_op = false;
    char index_raw[MAX_VAR_NAME_LEN] = "";

    if (tokens[1].type == TOKEN_VARIABLE) { 
        char temp_text[MAX_VAR_NAME_LEN * 2]; 
        strncpy(temp_text, var_name_token_text + 1, sizeof(temp_text)-1); temp_text[sizeof(temp_text)-1] = '\0';
        char* bracket = strchr(temp_text, '[');
        if (bracket) {
            char* end_bracket = strrchr(bracket, ']');
            if (end_bracket && end_bracket > bracket + 1) { 
                is_array_op = true;
                size_t base_len = bracket - temp_text;
                strncpy(var_name_or_base, temp_text, base_len); var_name_or_base[base_len] = '\0';
                size_t index_len = end_bracket - (bracket + 1);
                strncpy(index_raw, bracket + 1, index_len); index_raw[index_len] = '\0';
            } else { fprintf(stderr, "Malformed array index in %s: %s\n", increment ? "inc" : "dec", var_name_token_text); return; }
        } else { strncpy(var_name_or_base, temp_text, MAX_VAR_NAME_LEN -1); var_name_or_base[MAX_VAR_NAME_LEN-1] = '\0'; }
    } else { 
        strncpy(var_name_or_base, var_name_token_text, MAX_VAR_NAME_LEN -1); var_name_or_base[MAX_VAR_NAME_LEN-1] = '\0';
    }

    char* current_val_str;
    char expanded_index_for_array_op[INPUT_BUFFER_SIZE]; 

    if (is_array_op) {
        if (index_raw[0] == '"' && index_raw[strlen(index_raw)-1] == '"') {
            char unescaped_idx[INPUT_BUFFER_SIZE];
            unescape_string(index_raw, unescaped_idx, sizeof(unescaped_idx));
            expand_variables_in_string_advanced(unescaped_idx, expanded_index_for_array_op, sizeof(expanded_index_for_array_op));
        } else if (index_raw[0] == '$') {
            expand_variables_in_string_advanced(index_raw, expanded_index_for_array_op, sizeof(expanded_index_for_array_op));
        } else { 
            strncpy(expanded_index_for_array_op, index_raw, sizeof(expanded_index_for_array_op)-1);
            expanded_index_for_array_op[sizeof(expanded_index_for_array_op)-1] = '\0';
        }
        current_val_str = get_array_element_scoped(var_name_or_base, expanded_index_for_array_op);
    } else { 
        current_val_str = get_variable_scoped(var_name_or_base);
    }

    long current_val = 0;
    if (current_val_str) {
        char *endptr; errno = 0;
        current_val = strtol(current_val_str, &endptr, 10);
        if (errno != 0 || *current_val_str == '\0' || *endptr != '\0') {
            fprintf(stderr, "Warning: Variable/element '%s%s%s%s%s' ('%s') is not a valid integer for %s. Treating as 0.\n",
                tokens[1].type == TOKEN_VARIABLE ? "$" : "", var_name_or_base, 
                is_array_op ? "[" : "", is_array_op ? expanded_index_for_array_op : "", is_array_op ? "]" : "",
                current_val_str ? current_val_str : "NULL", increment ? "inc" : "dec");
            current_val = 0;
        }
    }
    current_val += (increment ? 1 : -1);
    char new_val_str[MAX_VAR_NAME_LEN]; 
    snprintf(new_val_str, sizeof(new_val_str), "%ld", current_val);

    if (is_array_op) set_array_element_scoped(var_name_or_base, expanded_index_for_array_op, new_val_str);
    else set_variable_scoped(var_name_or_base, new_val_str, false);
}

void handle_loadlib_statement(Token *tokens, int num_tokens) {
    // ... (remains the same)
    if (num_tokens != 3) { fprintf(stderr, "Syntax: loadlib <path_or_$var> <alias_or_$var>\n"); return; }
    if (current_exec_state == STATE_BLOCK_SKIP) return;
    char lib_path[MAX_FULL_PATH_LEN], alias[MAX_VAR_NAME_LEN];
    
    if (tokens[1].type == TOKEN_STRING) {
        char unescaped[INPUT_BUFFER_SIZE];
        unescape_string(tokens[1].text, unescaped, sizeof(unescaped));
        expand_variables_in_string_advanced(unescaped, lib_path, sizeof(lib_path));
    } else { 
        expand_variables_in_string_advanced(tokens[1].text, lib_path, sizeof(lib_path));
    }
    
    if (tokens[2].type == TOKEN_STRING) {
        char unescaped[INPUT_BUFFER_SIZE];
        unescape_string(tokens[2].text, unescaped, sizeof(unescaped));
        expand_variables_in_string_advanced(unescaped, alias, sizeof(alias));
    } else { 
        expand_variables_in_string_advanced(tokens[2].text, alias, sizeof(alias));
    }

    if (strlen(lib_path) == 0 || strlen(alias) == 0) { fprintf(stderr, "loadlib error: Path or alias is empty.\n"); return; }
    DynamicLib* current_lib = loaded_libs; while(current_lib) { if (strcmp(current_lib->alias, alias) == 0) { fprintf(stderr, "Error: Lib alias '%s' in use.\n", alias); return; } current_lib = current_lib->next; }
    void *handle = dlopen(lib_path, RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) { fprintf(stderr, "Error loading library '%s': %s\n", lib_path, dlerror()); return; }
    DynamicLib *new_lib_entry = (DynamicLib*)malloc(sizeof(DynamicLib));
    if (!new_lib_entry) { perror("malloc for new_lib_entry failed"); dlclose(handle); return; }
    strncpy(new_lib_entry->alias, alias, MAX_VAR_NAME_LEN -1); new_lib_entry->alias[MAX_VAR_NAME_LEN-1] = '\0';
    new_lib_entry->handle = handle; new_lib_entry->next = loaded_libs; loaded_libs = new_lib_entry;
}

void handle_calllib_statement(Token *tokens, int num_tokens) {
    // ... (remains the same)
    if (num_tokens < 3) { fprintf(stderr, "Syntax: calllib <alias> <func_name> [args...]\n"); return; }
    if (current_exec_state == STATE_BLOCK_SKIP) return;
    char alias[MAX_VAR_NAME_LEN], func_name[MAX_VAR_NAME_LEN];

    if (tokens[1].type == TOKEN_STRING) {
        char unescaped[INPUT_BUFFER_SIZE];
        unescape_string(tokens[1].text, unescaped, sizeof(unescaped));
        expand_variables_in_string_advanced(unescaped, alias, sizeof(alias));
    } else { 
        expand_variables_in_string_advanced(tokens[1].text, alias, sizeof(alias));
    }

    if (tokens[2].type == TOKEN_STRING) {
        char unescaped[INPUT_BUFFER_SIZE];
        unescape_string(tokens[2].text, unescaped, sizeof(unescaped));
        expand_variables_in_string_advanced(unescaped, func_name, sizeof(func_name));
    } else { 
        expand_variables_in_string_advanced(tokens[2].text, func_name, sizeof(func_name));
    }

    if (strlen(alias) == 0 || strlen(func_name) == 0) { fprintf(stderr, "calllib error: Alias or func name empty.\n"); return; }
    DynamicLib* lib_entry = loaded_libs; void* lib_handle = NULL;
    while(lib_entry) { if (strcmp(lib_entry->alias, alias) == 0) { lib_handle = lib_entry->handle; break; } lib_entry = lib_entry->next; }
    if (!lib_handle) { fprintf(stderr, "Error: Library alias '%s' not found.\n", alias); return; }
    dlerror(); void* func_ptr = dlsym(lib_handle, func_name); char* dlsym_error = dlerror();
    if (dlsym_error != NULL) { fprintf(stderr, "Error finding func '%s' in lib '%s': %s\n", func_name, alias, dlsym_error); return; }
    if (!func_ptr) { fprintf(stderr, "Error finding func '%s' (ptr NULL, no dlerror).\n", func_name); return; }

    typedef int (*lib_func_sig_t)(int, char**, char*, int); 
    lib_func_sig_t target_func = (lib_func_sig_t)func_ptr;
    int lib_argc = num_tokens - 3;
    char* lib_argv_expanded_storage[MAX_ARGS][INPUT_BUFFER_SIZE]; char* lib_argv[MAX_ARGS + 1];
    for(int i=0; i < lib_argc; ++i) {
        if (tokens[i+3].type == TOKEN_STRING) { char unescaped[INPUT_BUFFER_SIZE]; unescape_string(tokens[i+3].text, unescaped, sizeof(unescaped)); expand_variables_in_string_advanced(unescaped, lib_argv_expanded_storage[i], INPUT_BUFFER_SIZE);
        } else { expand_variables_in_string_advanced(tokens[i+3].text, lib_argv_expanded_storage[i], INPUT_BUFFER_SIZE); }
        lib_argv[i] = lib_argv_expanded_storage[i];
    } lib_argv[lib_argc] = NULL;
    char lib_output_buffer[INPUT_BUFFER_SIZE]; lib_output_buffer[0] = '\0';
    int lib_status = target_func(lib_argc, lib_argv, lib_output_buffer, sizeof(lib_output_buffer));
    char status_str[12]; snprintf(status_str, sizeof(status_str), "%d", lib_status);
    set_variable_scoped("LAST_LIB_CALL_STATUS", status_str, false);
    set_variable_scoped("LAST_LIB_CALL_OUTPUT", lib_output_buffer, false);
}

void handle_import_statement(Token *tokens, int num_tokens) {
    // ... (remains the same)
    if (current_exec_state == STATE_BLOCK_SKIP && current_exec_state != STATE_IMPORT_PARSING) { 
        return;
    }

    if (num_tokens < 2) {
        fprintf(stderr, "Syntax: import <module_name_or_path>\n");
        return;
    }

    char module_spec_expanded[MAX_FULL_PATH_LEN];
    if (tokens[1].type == TOKEN_STRING) {
        char unescaped_module_spec[MAX_FULL_PATH_LEN];
        unescape_string(tokens[1].text, unescaped_module_spec, sizeof(unescaped_module_spec));
        expand_variables_in_string_advanced(unescaped_module_spec, module_spec_expanded, sizeof(module_spec_expanded));
    } else { 
        expand_variables_in_string_advanced(tokens[1].text, module_spec_expanded, sizeof(module_spec_expanded));
    }
    
    if (strlen(module_spec_expanded) == 0) {
        fprintf(stderr, "Error: import statement received an empty module path/name after expansion.\n");
        return;
    }

    char full_module_path[MAX_FULL_PATH_LEN];
    if (find_module_in_path(module_spec_expanded, full_module_path)) {
        ExecutionState previous_exec_state = current_exec_state;
        current_exec_state = STATE_IMPORT_PARSING; 

        execute_script(full_module_path, true, false); 

        current_exec_state = previous_exec_state; 
    } else {
        fprintf(stderr, "Error: Module '%s' not found for import.\n", module_spec_expanded);
    }
}

void handle_update_cwd_statement(Token *tokens, int num_tokens) {
    // ... (remains the same)
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    if (num_tokens != 1) {
        fprintf(stderr, "Syntax: update_cwd (takes no arguments)\n");
        return;
    }

    char cwd_buffer[PATH_MAX];
    if (getcwd(cwd_buffer, sizeof(cwd_buffer)) != NULL) {
        set_variable_scoped("CWD", cwd_buffer, false);
    } else {
        perror("bsh: update_cwd: getcwd() error");
        set_variable_scoped("CWD", "", false); 
    }
}

// New handler for unary operations like $var++ or ++$var
void handle_unary_op_statement(Token* var_token, Token* op_token, bool is_prefix) {
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    char var_name_clean[MAX_VAR_NAME_LEN];
    // The var_token->text for a TOKEN_VARIABLE will be like "$myvar" or "${myvar}" or "$arr[idx]"
    // We need to extract the actual variable name part for the BSH handler.
    // For simplicity, this example will focus on simple variables like "$myvar".
    // Handling "$arr[idx]++" would require parsing the base name and index here.
    if (var_token->text[0] == '$') {
        if (var_token->text[1] == '{') { // ${varname}
            const char* end_brace = strchr(var_token->text + 2, '}');
            if (end_brace) {
                size_t len = end_brace - (var_token->text + 2);
                if (len < MAX_VAR_NAME_LEN) {
                    strncpy(var_name_clean, var_token->text + 2, len);
                    var_name_clean[len] = '\0';
                } else {
                    fprintf(stderr, "Error: Variable name in ${...} too long for unary op.\n");
                    return;
                }
            } else { // Malformed ${...
                fprintf(stderr, "Error: Malformed ${...} in unary op.\n");
                return;
            }
        } else { // $varname
             // Check for array access $var[index] - this part needs more robust parsing if to be supported directly.
            char* bracket_ptr = strchr(var_token->text + 1, '[');
            if (bracket_ptr) {
                fprintf(stderr, "Error: Unary operator on array element (e.g., $arr[idx]++) is not directly supported by this simple handler. Use 'inc $arr[idx]' or a BSH function.\n");
                // For a full implementation, you'd parse base_var_name and index_str_raw here,
                // then the BSH handler would need to be more complex or you'd have specialized BSH handlers.
                return;
            }
            strncpy(var_name_clean, var_token->text + 1, MAX_VAR_NAME_LEN - 1);
            var_name_clean[MAX_VAR_NAME_LEN - 1] = '\0';
        }
    } else {
        fprintf(stderr, "Error: Unary operator expected a variable (e.g., $var), got '%s'.\n", var_token->text);
        return;
    }
    
    if (strlen(var_name_clean) == 0) {
        fprintf(stderr, "Error: Empty variable name in unary operation.\n");
        return;
    }


    const char* op_str = op_token->text;
    char bsh_handler_name[MAX_VAR_NAME_LEN];

    if (is_prefix) {
        if (strcmp(op_str, "++") == 0) strncpy(bsh_handler_name, "__bsh_prefix_increment", sizeof(bsh_handler_name)-1);
        else if (strcmp(op_str, "--") == 0) strncpy(bsh_handler_name, "__bsh_prefix_decrement", sizeof(bsh_handler_name)-1);
        else { fprintf(stderr, "Internal error: Unknown prefix unary operator '%s'.\n", op_str); return; }
    } else { // Postfix
        if (strcmp(op_str, "++") == 0) strncpy(bsh_handler_name, "__bsh_postfix_increment", sizeof(bsh_handler_name)-1);
        else if (strcmp(op_str, "--") == 0) strncpy(bsh_handler_name, "__bsh_postfix_decrement", sizeof(bsh_handler_name)-1);
        else { fprintf(stderr, "Internal error: Unknown postfix unary operator '%s'.\n", op_str); return; }
    }
    bsh_handler_name[sizeof(bsh_handler_name)-1] = '\0';

    char c_result_buffer[INPUT_BUFFER_SIZE];
    const char* bsh_temp_result_var_name = "__TEMP_UNARY_OP_EXPR_RES"; // BSH var to hold expression's value

    // Call the BSH handler.
    // BSH handler signature: function handler_name (var_to_modify_name_str, result_holder_var_name_str)
    if (invoke_bsh_unary_op_call(bsh_handler_name, 
                                 var_name_clean,          // Pass the clean variable name (e.g., "myvar")
                                 bsh_temp_result_var_name, 
                                 c_result_buffer, sizeof(c_result_buffer))) {
        // The BSH handler performs the side effect (modifies var_name_clean)
        // AND sets bsh_temp_result_var_name to the "value" of the expression.
        set_variable_scoped("LAST_OP_RESULT", c_result_buffer, false);
        // If these operations should print their result when standalone:
        // printf("%s\n", c_result_buffer); 
    } else {
        fprintf(stderr, "Error executing BSH unary op handler '%s' for variable '%s'.\n", bsh_handler_name, var_name_clean);
        set_variable_scoped("LAST_OP_RESULT", "UNARY_OP_HANDLER_ERROR", false);
    }
}


// --- Block Management ---
// ... (push_block_bf, pop_block_bf, peek_block_bf, handle_opening_brace_token, handle_closing_brace_token remain the same)
void push_block_bf(BlockType type, bool condition_true, long loop_start_fpos, int loop_start_line_no) {
    if (block_stack_top_bf >= MAX_NESTING_DEPTH - 1) { fprintf(stderr, "Max block nesting depth exceeded.\n"); return; }
    block_stack_top_bf++;
    block_stack[block_stack_top_bf].type = type;
    block_stack[block_stack_top_bf].condition_true = condition_true;
    block_stack[block_stack_top_bf].loop_start_fpos = loop_start_fpos;
    block_stack[block_stack_top_bf].loop_start_line_no = loop_start_line_no;
    block_stack[block_stack_top_bf].prev_exec_state = current_exec_state;
}

BlockFrame* pop_block_bf() {
    if (block_stack_top_bf < 0) { return NULL; }
    return &block_stack[block_stack_top_bf--];
}

BlockFrame* peek_block_bf() {
    if (block_stack_top_bf < 0) return NULL;
    return &block_stack[block_stack_top_bf];
}

void handle_opening_brace_token(Token token) {
    BlockFrame* current_block_frame = peek_block_bf();
    if (!current_block_frame) { 
        if (is_defining_function && current_function_definition && current_exec_state != STATE_BLOCK_SKIP) {
            push_block_bf(BLOCK_TYPE_FUNCTION_DEF, true, 0, 0); 
            return;
        }
        fprintf(stderr, "Error: '{' found without a preceding statement expecting it.\n"); return;
    }
    if (current_block_frame->type == BLOCK_TYPE_FUNCTION_DEF) { 
    }
    else if (current_block_frame->condition_true && current_exec_state != STATE_BLOCK_SKIP) current_exec_state = STATE_BLOCK_EXECUTE;
    else current_exec_state = STATE_BLOCK_SKIP;
}

void handle_closing_brace_token(Token token, FILE* input_source) {
    BlockFrame* closed_block_frame = pop_block_bf();
    if (!closed_block_frame) { fprintf(stderr, "Error: '}' found without a matching open block.\n"); current_exec_state = STATE_NORMAL; return; }

    ExecutionState state_before_closed_block = closed_block_frame->prev_exec_state;
    BlockFrame* parent_block = peek_block_bf(); 

    if (closed_block_frame->type == BLOCK_TYPE_WHILE && closed_block_frame->condition_true && 
        (current_exec_state == STATE_BLOCK_EXECUTE || current_exec_state == STATE_NORMAL || current_exec_state == STATE_IMPORT_PARSING) ) { 
        
        bool can_loop_via_fseek = false;
        if (input_source_is_file(input_source) && closed_block_frame->loop_start_fpos != -1) {
             // Before seeking, re-evaluate the condition. This requires re-tokenizing the while header.
             // This is a complex part. A simpler (but less flexible) model is to just seek.
             // For now, we'll stick to the seek model, assuming the condition might change due to side effects in the loop.
            if (fseek(input_source, closed_block_frame->loop_start_fpos, SEEK_SET) == 0) {
                can_loop_via_fseek = true;
                current_exec_state = STATE_NORMAL; // Allow re-processing of the while line by execute_script
                return; 
            } else { 
                perror("fseek failed for while loop"); 
            }
        } else if (!input_source_is_file(input_source) && closed_block_frame->loop_start_line_no > 0) { 
             // This case is for loops inside function bodies (not read from file)
             // True looping here would require re-executing the function lines from the loop header.
             // This is not implemented by simple fseek.
             fprintf(stderr, "Warning: 'while' loop repetition for non-file input (e.g. function body, line %d) is not supported by fseek. Loop will terminate.\n", closed_block_frame->loop_start_line_no);
        }
    }

    if (!parent_block) { 
        current_exec_state = STATE_NORMAL;
    } else { 
        if (parent_block->type == BLOCK_TYPE_FUNCTION_DEF && is_defining_function) {
            current_exec_state = STATE_DEFINE_FUNC_BODY; 
        } else if (parent_block->condition_true) {
            current_exec_state = STATE_BLOCK_EXECUTE; 
        } else {
            current_exec_state = STATE_BLOCK_SKIP; 
        }
    }

    if (closed_block_frame->type == BLOCK_TYPE_FUNCTION_DEF) {
        if (current_function_definition) { 
            current_function_definition->next = function_list; 
            function_list = current_function_definition;
            current_function_definition = NULL; 
        }
        is_defining_function = false; 
        current_exec_state = state_before_closed_block; 
        
        if (!parent_block && current_exec_state == STATE_DEFINE_FUNC_BODY) {
            current_exec_state = STATE_NORMAL;
        }
    }
    
    if (block_stack_top_bf == -1 && current_exec_state != STATE_DEFINE_FUNC_BODY) {
        current_exec_state = STATE_NORMAL;
    }
}

void handle_exit_statement(Token *tokens, int num_tokens) {
    if (current_exec_state == STATE_BLOCK_SKIP && current_exec_state != STATE_IMPORT_PARSING) {
         // If skipping, an exit within that block context might also be skipped,
         // or it might immediately terminate the script. Forcing termination is common.
    }

    // For now, 'exit' without args means exit current script/function with status 0.
    // If it's the main interactive shell, it exits the shell.
    // A more advanced 'exit' could take a status code.
    // And distinguish between exiting a function vs. exiting the whole script.

    // This simple version sets the return request state,
    // which will stop current script/function processing.
    // If called from the top-level interactive loop, main() would handle it.
    bsh_last_return_value[0] = '\0'; // 'exit' itself doesn't set a printable return value here
    bsh_return_value_is_set = false; // 'exit' is about termination status, not typical 'return value' for echo

    if (num_tokens > 1) { // exit <status_code>
        char expanded_status[INPUT_BUFFER_SIZE];
        expand_variables_in_string_advanced(tokens[1].text, expanded_status, sizeof(expanded_status));
        long exit_code = strtol(expanded_status, NULL, 10);
        // Store this exit_code somewhere if the shell needs to propagate it as actual process exit status.
        // For now, we'll just use it to signal return.
        snprintf(bsh_last_return_value, sizeof(bsh_last_return_value), "%ld", exit_code);
        bsh_return_value_is_set = true; // For script result capture
    }

    current_exec_state = STATE_RETURN_REQUESTED; // Use the same state to stop execution
                                                 // The main loop or script executor needs to check this
                                                 // and decide if it's a full shell exit or script/func exit.

    // If this is the top-level interactive shell, the main loop in main()
    // would see STATE_RETURN_REQUESTED and then decide to actually exit the bsh process.
    // If in a script, execute_script() would stop.
    // If in a function, execute_user_function() would stop.
}

void handle_eval_statement(Token *tokens, int num_tokens) {
    if (current_exec_state == STATE_BLOCK_SKIP && current_exec_state != STATE_IMPORT_PARSING) {
        return; // Don't eval if in a skipped block (unless it's an import context that allows it)
    }

    if (num_tokens < 2) {
        // 'eval' with no arguments is typically a no-op or might return success.
        // Some shells might error; for now, let's make it a no-op.
        set_variable_scoped("LAST_COMMAND_STATUS", "0", false);
        return;
    }

    char code_to_eval[MAX_LINE_LENGTH * 2]; // Buffer for the string to be evaluated
                                           // Potentially needs to be larger if eval arguments are very long
    code_to_eval[0] = '\0';
    size_t current_eval_code_len = 0;

    // Concatenate all arguments to 'eval' into a single string, expanding them.
    for (int i = 1; i < num_tokens; i++) {
        if (tokens[i].type == TOKEN_COMMENT) break;

        char expanded_arg_part[INPUT_BUFFER_SIZE];
        if (tokens[i].type == TOKEN_STRING) {
            char unescaped_val[INPUT_BUFFER_SIZE];
            unescape_string(tokens[i].text, unescaped_val, sizeof(unescaped_val)); // From your bsh.c
            expand_variables_in_string_advanced(unescaped_val, expanded_arg_part, sizeof(expanded_arg_part)); // From your bsh.c
        } else {
            expand_variables_in_string_advanced(tokens[i].text, expanded_arg_part, sizeof(expanded_arg_part)); // From your bsh.c
        }

        size_t part_len = strlen(expanded_arg_part);
        if (current_eval_code_len + part_len + (i > 1 ? 1 : 0) < sizeof(code_to_eval)) {
            if (i > 1) { // Add space between arguments
                strcat(code_to_eval, " ");
                current_eval_code_len++;
            }
            strcat(code_to_eval, expanded_arg_part);
            current_eval_code_len += part_len;
        } else {
            fprintf(stderr, "eval: Constructed code string too long.\n");
            set_variable_scoped("LAST_COMMAND_STATUS", "1", false); // Indicate error
            return;
        }
    }

    if (strlen(code_to_eval) > 0) {
        // printf("[DEBUG eval: Executing: \"%s\"]\n", code_to_eval); // Optional debug

        // Store current line number and input source, as eval provides its own "line"
        // FILE* original_input_source = input_source; // from process_line params (if needed)
        // int original_line_no = current_line_no;     // from process_line params (if needed)

        // Execute the constructed string.
        // The `process_line` function is designed to handle a single line of BSH code.
        // `input_source` is NULL because this code doesn't come from a seekable file stream for `while` loops.
        // `current_line_no` can be set to 0 or 1 for the context of the eval'd string.
        // The `exec_mode_param` should be STATE_NORMAL, as eval'd code should execute normally
        // within the current block and scope context.
        process_line(code_to_eval, NULL, 0, STATE_NORMAL);

        // Restore original line_no/input_source if they were modified by process_line or its callees
        // (This depends on how `process_line` uses these for context in loops, etc.)
        // Generally, process_line for eval should not impact the outer script's file seeking.
    } else {
        set_variable_scoped("LAST_COMMAND_STATUS", "0", false); // Eval of empty string is success
    }
}

/////
/////
/////

// --- Utility Implementations ---
// ... (trim_whitespace, free_function_list, free_operator_list, free_loaded_libs, get_file_pos, unescape_string, input_source_is_file remain the same)
char* trim_whitespace(char *str) {
    if (!str) return NULL; char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str; 
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

void free_function_list() {
    UserFunction *current = function_list; UserFunction *next_func;
    while (current != NULL) {
        next_func = current->next;
        for (int i = 0; i < current->line_count; ++i) if(current->body[i]) free(current->body[i]);
        free(current); current = next_func;
    }
    function_list = NULL;
}

void free_operator_list() {
    OperatorDefinition *current = operator_list_head; OperatorDefinition *next_op;
    while (current) { next_op = current->next; free(current); current = next_op; }
    operator_list_head = NULL;
}

void free_loaded_libs() {
    DynamicLib *current = loaded_libs; DynamicLib *next_lib;
    while(current) {
        next_lib = current->next;
        if (current->handle) dlclose(current->handle);
        free(current); current = next_lib;
    }
    loaded_libs = NULL;
}

long get_file_pos(FILE* f) {
    if (!f || f == stdin || f == stdout || f == stderr) return -1L;
    long pos = ftell(f);
    if (pos == -1L) { return -1L; }
    return pos;
}

char* unescape_string(const char* input_raw, char* output_buffer, size_t buffer_size) {
    char* out = output_buffer; const char* p = input_raw; size_t out_len = 0;
    bool in_quotes = false;

    if (*p == '"') { 
        p++; 
        in_quotes = true;
    }

    while (*p && out_len < buffer_size - 1) {
        if (in_quotes && *p == '"' && !(p > input_raw && *(p-1) == '\\' && (p-2 < input_raw || *(p-2) != '\\'))) {
             break; 
        }
        if (*p == '\\') {
            p++; if (!*p) break; 
            switch (*p) {
                case 'n': *out++ = '\n'; break; case 't': *out++ = '\t'; break;
                case '"': *out++ = '"'; break;  case '\\': *out++ = '\\'; break;
                case '$': *out++ = '$'; break;  default: *out++ = '\\'; *out++ = *p; break; 
            }
        } else { *out++ = *p; }
        if (*p) p++; 
        out_len++;
    }
    *out = '\0';
    return output_buffer;
}

bool input_source_is_file(FILE* f) {
    if (!f || f == stdin || f == stdout || f == stderr) return false;
    int fd = fileno(f);
    if (fd == -1) return false; 
    return (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO);
}

void execute_script(const char *filename, bool is_import_call, bool is_startup_script) {
    // ... (remains largely the same, ensure loop_start_fpos is correctly passed if used by while)
    FILE *script_file = fopen(filename, "r");
    if (!script_file) {
        if (!is_startup_script || errno != ENOENT) { 
            fprintf(stderr, "Error opening script '%s': %s\n", filename, strerror(errno));
        }
        return;
    }
    
    char line_buffer[INPUT_BUFFER_SIZE]; int line_no = 0;
    ExecutionState script_exec_mode = is_import_call ? STATE_IMPORT_PARSING : STATE_NORMAL;

    ExecutionState outer_exec_state_backup = current_exec_state;
    int outer_block_stack_top_bf_backup = block_stack_top_bf;
    bool restore_context = (!is_import_call && !is_startup_script);

    while (true) {
        if (!fgets(line_buffer, sizeof(line_buffer), script_file)) {
            if (feof(script_file)) break; 
            if (ferror(script_file)) { perror("Error reading script file"); break; }
        }
        line_no++;
        process_line(line_buffer, script_file, line_no, script_exec_mode);
    }
    fclose(script_file);

    if (is_import_call) { 
        if (is_defining_function && current_function_definition) {
            fprintf(stderr, "Warning: Unterminated function definition '%s' at end of imported file '%s'.\n", current_function_definition->name, filename);
            for(int i=0; i < current_function_definition->line_count; ++i) if(current_function_definition->body[i]) free(current_function_definition->body[i]);
            free(current_function_definition); current_function_definition = NULL; is_defining_function = false;
            if (block_stack_top_bf >=0 && peek_block_bf() && peek_block_bf()->type == BLOCK_TYPE_FUNCTION_DEF) {
                pop_block_bf();
            }
        }
    } else if (restore_context) { 
        current_exec_state = outer_exec_state_backup;
        while(block_stack_top_bf > outer_block_stack_top_bf_backup) {
            BlockFrame* bf = pop_block_bf();
            fprintf(stderr, "Warning: Script '%s' ended with unclosed block (type %d).\n", filename, bf ? bf->type : -1);
        }
    }

    if (is_startup_script) {
        current_exec_state = STATE_NORMAL;
        while(block_stack_top_bf > -1) { 
             BlockFrame* bf = pop_block_bf();
             if (bf && bf->type == BLOCK_TYPE_FUNCTION_DEF && is_defining_function) {
                fprintf(stderr, "Warning: Startup script ended with unterminated function definition.\n");
                if(current_function_definition) {
                    for(int i=0; i < current_function_definition->line_count; ++i) if(current_function_definition->body[i]) free(current_function_definition->body[i]);
                    free(current_function_definition); current_function_definition = NULL;
                }
                is_defining_function = false;
             }
        }
    }
}

///
/// Objects (JSON-like)
///

// Helper to skip whitespace in the object string
const char* skip_whitespace_in_obj_str(const char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

// Helper to parse a BSH-style quoted string from the object string
// Returns a pointer to the character after the parsed string (and closing quote).
// Stores the unescaped string content in 'dest_buffer'.
// This is a simplified string parser for this specific context.
const char* parse_quoted_string_from_obj_str(const char* s, char* dest_buffer, size_t buffer_size) {
    dest_buffer[0] = '\0';
    s = skip_whitespace_in_obj_str(s);
    if (*s != '"') return s; // Expected opening quote

    s++; // Skip opening quote
    char* out = dest_buffer;
    size_t count = 0;
    while (*s && count < buffer_size - 1) {
        if (*s == '"') { // End of string
            s++; // Skip closing quote
            *out = '\0';
            return s;
        }
        // Basic escape handling (add more if needed: \n, \t, etc.)
        if (*s == '\\' && *(s + 1) != '\0') {
            s++;
            if (*s == '"' || *s == '\\') {
                *out++ = *s++;
            } else { // Keep backslash if not a recognized escape for this simple parser
                *out++ = '\\';
                *out++ = *s++;
            }
        } else {
            *out++ = *s++;
        }
        count++;
    }
    *out = '\0'; // Ensure null termination
    // If loop ended due to buffer full or end of string without closing quote, it's an error
    // or implies the string was truncated. For simplicity, we return current 's'.
    return s;
}

// Main recursive parsing function
// data_ptr is a pointer-to-pointer to traverse the input string
void parse_bsh_object_recursive(const char** data_ptr, const char* current_base_bsh_var_name, int scope_id) {
    const char* p = skip_whitespace_in_obj_str(*data_ptr);

    if (*p != '[') {
        fprintf(stderr, "BSH Object Parse Error: Expected '[' for object/array start. At: %s\n", p);
        // To prevent infinite loops on malformed input, consume something or error out.
        // For simplicity, we'll try to find the end or a known delimiter if things go wrong.
        *data_ptr = p + strlen(p); // Consume rest of string on error
        return;
    }
    p++; // Consume '['

    bool first_element = true;
    while (*p) {
        p = skip_whitespace_in_obj_str(p);
        if (*p == ']') {
            p++; // Consume ']'
            break; // End of current object/array
        }

        if (!first_element) {
            if (*p == ',') {
                p++; // Consume ','
                p = skip_whitespace_in_obj_str(p);
            } else {
                fprintf(stderr, "BSH Object Parse Error: Expected ',' or ']' between elements. At: %s\n", p);
                *data_ptr = p + strlen(p); return; // Error
            }
        }
        first_element = false;

        // Parse Key
        char key_buffer[MAX_VAR_NAME_LEN]; // For "0", "ciao"
        p = parse_quoted_string_from_obj_str(p, key_buffer, sizeof(key_buffer));
        if (strlen(key_buffer) == 0) {
            fprintf(stderr, "BSH Object Parse Error: Expected valid key string. At: %s\n", p);
            *data_ptr = p + strlen(p); return; // Error
        }

        p = skip_whitespace_in_obj_str(p);
        if (*p != ':') {
            fprintf(stderr, "BSH Object Parse Error: Expected ':' after key '%s'. At: %s\n", key_buffer, p);
            *data_ptr = p + strlen(p); return; // Error
        }
        p++; // Consume ':'
        p = skip_whitespace_in_obj_str(p);

        // Construct new base name for BSH variable
        char next_base_bsh_var_name[MAX_VAR_NAME_LEN * 2]; // Increased size for nested names
        // Sanitize key_buffer for use in variable names (e.g., replace disallowed chars with '_')
        // For now, assume keys are simple enough or BSH var names allow them.
        snprintf(next_base_bsh_var_name, sizeof(next_base_bsh_var_name), "%s_%s", current_base_bsh_var_name, key_buffer);

        // Parse Value
        if (*p == '[') { // Nested object/array
            // Set a type for the current key indicating it's a nested structure
            char type_var_for_key[MAX_VAR_NAME_LEN * 2 + 20];
            snprintf(type_var_for_key, sizeof(type_var_for_key), "%s_BSH_STRUCT_TYPE", next_base_bsh_var_name);
            set_variable_scoped(type_var_for_key, "BSH_OBJECT", false); // Using current scope

            parse_bsh_object_recursive(&p, next_base_bsh_var_name, scope_id);
        } else if (*p == '"') { // String value
            char value_buffer[INPUT_BUFFER_SIZE]; // Assuming values fit here
            p = parse_quoted_string_from_obj_str(p, value_buffer, sizeof(value_buffer));
            set_variable_scoped(next_base_bsh_var_name, value_buffer, false); // Using current scope
        } else {
            fprintf(stderr, "BSH Object Parse Error: Expected value (string or nested object) after key '%s'. At: %s\n", key_buffer, p);
            *data_ptr = p + strlen(p); return; // Error
        }
    } // End while
    *data_ptr = p; // Update the main pointer
}

// The public function called by handle_assignment_advanced
void parse_and_flatten_bsh_object_string(const char* object_data_string, const char* base_var_name, int current_scope_id) {
    const char* p = object_data_string; // p will be advanced by the recursive parser

    // Set a root type for the base variable name
    char root_type_var_key[MAX_VAR_NAME_LEN + 30]; // Enough for _BSH_STRUCT_TYPE and safety
    snprintf(root_type_var_key, sizeof(root_type_var_key), "%s_BSH_STRUCT_TYPE", base_var_name);
    set_variable_scoped(root_type_var_key, "BSH_OBJECT_ROOT", false); // Or "BSH_ARRAY_ROOT"

    // Temporarily push the target scope if different from current, or ensure set_variable_scoped uses it.
    // For simplicity, we assume set_variable_scoped in our recursive calls will use the active scope
    // which should be the one where the assignment is happening.
    // If 'current_scope_id' is different from scope_stack[scope_stack_top].scope_id,
    // you might need a temporary scope push/pop or pass scope_id to set_variable_scoped.
    // The current set_variable_scoped uses scope_stack[scope_stack_top].scope_id implicitly.

    parse_bsh_object_recursive(&p, base_var_name, current_scope_id);

    // p should now point to the end of the parsed structure or where parsing stopped.
    // You can check if *p is whitespace or null to see if the whole string was consumed.
    p = skip_whitespace_in_obj_str(p);
    if (*p != '\0') {
        fprintf(stderr, "BSH Object Parse Warning: Extra characters found after main object structure. At: %s\n", p);
    }
    fprintf(stdout, "[BSH_DEBUG] Flattening complete for base var '%s'.\n", base_var_name);
}

/// Stringify object

// Helper for stringification: find all variables prefixed by base_var_name_
// This is a conceptual helper, actual iteration would be over variable_list_head.
typedef struct VarPair { char key[MAX_VAR_NAME_LEN]; char* value; char type_info[MAX_VAR_NAME_LEN]; struct VarPair* next; } VarPair;

// Recursive helper
bool build_object_string_recursive(const char* current_base_name, char** p_out, size_t* remaining_size, int scope_id) {
    char prefix_pattern[MAX_VAR_NAME_LEN * 2];
    snprintf(prefix_pattern, sizeof(prefix_pattern), "%s_", current_base_name);
    size_t prefix_len = strlen(prefix_pattern);

    VarPair* pairs_head = NULL;
    VarPair* pairs_tail = NULL;
    int element_count = 0;

    // Step 1: Collect all direct children of current_base_name in the current scope
    Variable* var_node = variable_list_head;
    while (var_node) {
        if (var_node->scope_id == scope_id && strncmp(var_node->name, prefix_pattern, prefix_len) == 0) {
            const char* sub_key_full = var_node->name + prefix_len;
            // Ensure this is a direct child, not a grandchild (e.g., base_key1_subkey vs base_key1)
            if (strchr(sub_key_full, '_') == NULL || 
                (strstr(sub_key_full, "_BSH_STRUCT_TYPE") != NULL && strchr(sub_key_full, '_') == strstr(sub_key_full, "_BSH_STRUCT_TYPE")) ) {
                
                char actual_key[MAX_VAR_NAME_LEN];
                strncpy(actual_key, sub_key_full, sizeof(actual_key)-1);
                actual_key[sizeof(actual_key)-1] = '\0';
                
                char* type_suffix_ptr = strstr(actual_key, "_BSH_STRUCT_TYPE");
                if (type_suffix_ptr) { // It's a type variable for a sub-object, not a direct value itself (unless it's the root)
                    *type_suffix_ptr = '\0'; // Get the key name before "_BSH_STRUCT_TYPE"
                }

                // Avoid adding duplicates if we process type var and value var separately
                bool key_already_added = false;
                for(VarPair* vp = pairs_head; vp; vp = vp->next) { if(strcmp(vp->key, actual_key) == 0) {key_already_added = true; break;}}
                if(key_already_added && !type_suffix_ptr) continue; // If value var and key is added from type, skip
                if(key_already_added && type_suffix_ptr) { // Update type if key exists
                     for(VarPair* vp = pairs_head; vp; vp = vp->next) { if(strcmp(vp->key, actual_key) == 0) { strncpy(vp->type_info, var_node->value, sizeof(vp->type_info)-1); break;}}
                     continue;
                }


                VarPair* new_pair = (VarPair*)malloc(sizeof(VarPair));
                if (!new_pair) { /* error */ return false; }
                strncpy(new_pair->key, actual_key, sizeof(new_pair->key)-1);
                new_pair->key[sizeof(new_pair->key)-1] = '\0';
                new_pair->value = NULL; // Will be filled if it's a direct value
                new_pair->type_info[0] = '\0'; // Default no specific type
                new_pair->next = NULL;

                if (type_suffix_ptr) { // This was a *_BSH_STRUCT_TYPE variable
                    strncpy(new_pair->type_info, var_node->value, sizeof(new_pair->type_info)-1);
                } else { // This is a direct value variable
                    new_pair->value = var_node->value;
                }
                
                if (!pairs_head) pairs_head = pairs_tail = new_pair;
                else { pairs_tail->next = new_pair; pairs_tail = new_pair; }
                element_count++;
            }
        }
        var_node = var_node->next;
    }
    
    // Step 1b: For keys found via _BSH_STRUCT_TYPE, find their actual value if they are simple
    // (This logic might need refinement if a key is ONLY defined by its _BSH_STRUCT_TYPE but has no direct value, implying it's purely a container)
    for(VarPair* vp = pairs_head; vp; vp = vp->next) {
        if (vp->value == NULL && strlen(vp->type_info) == 0) { // No value and no type, try to get direct value
             char direct_value_var_name[MAX_VAR_NAME_LEN *2];
             snprintf(direct_value_var_name, sizeof(direct_value_var_name), "%s_%s", current_base_name, vp->key);
             vp->value = get_variable_scoped(direct_value_var_name); // Relies on correct scope
        }
    }


    // Step 2: Append '['
    if (*remaining_size < 2) return false;
    **p_out = '['; (*p_out)++; (*remaining_size)--;

    // Step 3: Iterate through collected pairs and stringify them
    // TODO: Sort pairs if necessary (e.g., numeric keys for array-like objects)
    bool first = true;
    VarPair* current_pair = pairs_head;
    while (current_pair) {
        if (!first) {
            if (*remaining_size < 2) { /* free VarPairs */ return false; }
            **p_out = ','; (*p_out)++;
            **p_out = ' '; (*p_out)++; // Optional space
            (*remaining_size) -= 2;
        }
        first = false;

        // Append key (quoted)
        if (*remaining_size < strlen(current_pair->key) + 3) { /* free VarPairs */ return false; }
        **p_out = '"'; (*p_out)++; (*remaining_size)--;
        strcpy(*p_out, current_pair->key); (*p_out) += strlen(current_pair->key); (*remaining_size) -= strlen(current_pair->key);
        **p_out = '"'; (*p_out)++; (*remaining_size)--;

        // Append ':'
        if (*remaining_size < 2) { /* free VarPairs */ return false; }
        **p_out = ':'; (*p_out)++;
        **p_out = ' '; (*p_out)++; // Optional space
        (*remaining_size) -= 2;

        // Append value
        char next_level_base_name[MAX_VAR_NAME_LEN * 2];
        snprintf(next_level_base_name, sizeof(next_level_base_name), "%s_%s", current_base_name, current_pair->key);

        if (strcmp(current_pair->type_info, "BSH_OBJECT") == 0 || strcmp(current_pair->type_info, "BSH_OBJECT_ROOT") == 0) { // It's a nested object
            if (!build_object_string_recursive(next_level_base_name, p_out, remaining_size, scope_id)) {
                 /* free VarPairs */ return false;
            }
        } else if (current_pair->value) { // Simple string value
            if (*remaining_size < strlen(current_pair->value) + 3) { /* free VarPairs */ return false; }
            **p_out = '"'; (*p_out)++; (*remaining_size)--;
            // TODO: Escape special characters in current_pair->value before strcpy
            strcpy(*p_out, current_pair->value);
            (*p_out) += strlen(current_pair->value);
            (*remaining_size) -= strlen(current_pair->value);
            **p_out = '"'; (*p_out)++; (*remaining_size)--;
        } else { // Should have a value or be a known container type
            if (*remaining_size < 3) { /* free VarPairs */ return false; }
            strcpy(*p_out, "\"\""); (*p_out) += 2; (*remaining_size) -=2; // Empty string if no value found
        }
        current_pair = current_pair->next;
    }

    // Step 4: Append ']'
    if (*remaining_size < 2) { /* free VarPairs */ return false; }
    **p_out = ']'; (*p_out)++; (*remaining_size)--;
    **p_out = '\0';

    // Free the collected VarPair list
    current_pair = pairs_head;
    while(current_pair) {
        VarPair* next = current_pair->next;
        free(current_pair);
        current_pair = next;
    }
    return true;
}


bool stringify_bsh_object_to_string(const char* base_var_name, char* output_buffer, size_t buffer_size) {
    output_buffer[0] = '\0';
    if (buffer_size < strlen("object:[]") + 1) return false; // Min possible output

    strcpy(output_buffer, "object:");
    char* p_out = output_buffer + strlen("object:");
    size_t remaining_size = buffer_size - strlen("object:") -1 /* for null terminator */;
    
    int current_scope = (scope_stack_top >= 0) ? scope_stack[scope_stack_top].scope_id : GLOBAL_SCOPE_ID;

    if (!build_object_string_recursive(base_var_name, &p_out, &remaining_size, current_scope)) {
        // Append error marker if something went wrong during build
        strcat(output_buffer, "[ERROR_DURING_STRINGIFY]");
        return false;
    }
    
    return true;
}

// echo definition moved here for helpers

void handle_echo_advanced(Token *tokens, int num_tokens) {
    if (current_exec_state == STATE_BLOCK_SKIP) return;

    char expanded_arg_buffer[INPUT_BUFFER_SIZE]; // Buffer for general argument expansion
    char object_stringified_buffer[INPUT_BUFFER_SIZE * 2]; // Potentially larger for object stringification

    for (int i = 1; i < num_tokens; i++) {
        if (tokens[i].type == TOKEN_COMMENT) break;

        const char* string_to_print = NULL;
        bool is_bsh_object_to_stringify = false;

        // First, determine if the argument is a variable that might be a BSH object
        if (tokens[i].type == TOKEN_VARIABLE) {
            char var_name_raw[MAX_VAR_NAME_LEN];
            // Extract clean variable name (without '$' or '${}')
            // This part needs to be robust as in handle_assignment_advanced or handle_unary_op_statement
            if (tokens[i].text[0] == '$') {
                if (tokens[i].text[1] == '{') {
                    const char* end_brace = strchr(tokens[i].text + 2, '}');
                    if (end_brace) {
                        size_t len = end_brace - (tokens[i].text + 2);
                        if (len < MAX_VAR_NAME_LEN) {
                            strncpy(var_name_raw, tokens[i].text + 2, len);
                            var_name_raw[len] = '\0';
                        } else { /* var name too long, handle error or truncate */ var_name_raw[0] = '\0'; }
                    } else { /* malformed */ var_name_raw[0] = '\0'; }
                } else {
                    // Simple $var or $var[index] - for echo, we care about the base var for type check
                    char* bracket_ptr = strchr(tokens[i].text + 1, '[');
                    if (bracket_ptr) {
                        size_t base_len = bracket_ptr - (tokens[i].text + 1);
                        if (base_len < MAX_VAR_NAME_LEN) {
                            strncpy(var_name_raw, tokens[i].text + 1, base_len);
                            var_name_raw[base_len] = '\0';
                        } else { var_name_raw[0] = '\0';}
                    } else {
                        strncpy(var_name_raw, tokens[i].text + 1, MAX_VAR_NAME_LEN - 1);
                        var_name_raw[MAX_VAR_NAME_LEN - 1] = '\0';
                    }
                }

                if (strlen(var_name_raw) > 0) {
                    char object_type_var_name[MAX_VAR_NAME_LEN + 30];
                    snprintf(object_type_var_name, sizeof(object_type_var_name), "%s_BSH_STRUCT_TYPE", var_name_raw);
                    char* struct_type = get_variable_scoped(object_type_var_name);

                    if (struct_type && strcmp(struct_type, "BSH_OBJECT_ROOT") == 0) {
                        // It's a BSH object, attempt to stringify it
                        if (stringify_bsh_object_to_string(var_name_raw, object_stringified_buffer, sizeof(object_stringified_buffer))) {
                            string_to_print = object_stringified_buffer;
                            is_bsh_object_to_stringify = true;
                        } else {
                            // Stringification failed, print an error marker or the raw variable token
                            snprintf(expanded_arg_buffer, sizeof(expanded_arg_buffer), "[Error stringifying object: %s]", var_name_raw);
                            string_to_print = expanded_arg_buffer;
                        }
                    }
                }
            }
        }

        if (!is_bsh_object_to_stringify) {
            // Not a BSH object or not a variable, so expand normally
            if (tokens[i].type == TOKEN_STRING) {
                char unescaped_val[INPUT_BUFFER_SIZE];
                unescape_string(tokens[i].text, unescaped_val, sizeof(unescaped_val));
                expand_variables_in_string_advanced(unescaped_val, expanded_arg_buffer, sizeof(expanded_arg_buffer));
            } else {
                expand_variables_in_string_advanced(tokens[i].text, expanded_arg_buffer, sizeof(expanded_arg_buffer));
            }
            string_to_print = expanded_arg_buffer;
        }

        printf("%s%s", string_to_print,
               (i == num_tokens - 1 || (i + 1 < num_tokens && tokens[i + 1].type == TOKEN_COMMENT)) ? "" : " ");
    }
    printf("\n");
}