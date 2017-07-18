#include <tree_sitter/parser.h>
#include <string>
#include <cwctype>

namespace {

using std::string;

enum TokenType {
  SIMPLE_HEREDOC,
  HEREDOC_BEGINNING,
  HEREDOC_MIDDLE,
  HEREDOC_END,
  FILE_DESCRIPTOR,
  EMPTY_VALUE,
  CONCAT,
  VARIABLE_NAME,
  NEWLINE,
};

struct Scanner {
  void skip(TSLexer *lexer) {
    lexer->advance(lexer, true);
  }

  void advance(TSLexer *lexer) {
    lexer->advance(lexer, false);
  }

  void reset() {}

  unsigned serialize(char *buffer) {
    unsigned length = heredoc_delimiter.size();
    if (length < TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
      memcpy(buffer, heredoc_delimiter.data(), length);
      return length;
    } else {
      return 0;
    }
  }

  void deserialize(const char *buffer, unsigned length) {
    heredoc_delimiter.assign(buffer, buffer + length);
  }

  bool scan_heredoc_end_identifier(TSLexer *lexer) {
    current_leading_word.clear();
    while (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
      current_leading_word += lexer->lookahead;
      advance(lexer);
    }
    return current_leading_word == heredoc_delimiter;
  }

  bool scan_heredoc_content(TSLexer *lexer, TokenType middle_type, TokenType end_type) {
    bool did_advance = false;

    for (;;) {
      switch (lexer->lookahead) {
        case '\0': {
          lexer->result_symbol = end_type;
          heredoc_delimiter.clear();
          return true;
        }

        case '$': {
          lexer->result_symbol = middle_type;
          return did_advance;
        }

        case '\n': {
          did_advance = true;
          advance(lexer);
          if (scan_heredoc_end_identifier(lexer)) {
            lexer->result_symbol = end_type;
            heredoc_delimiter.clear();
            return true;
          }
          break;
        }

        default: {
          did_advance = true;
          advance(lexer);
          break;
        }
      }
    }
  }

  bool scan(TSLexer *lexer, const bool *valid_symbols) {
    if (valid_symbols[CONCAT]) {
      if (!(
        iswspace(lexer->lookahead) ||
        lexer->lookahead == '>' ||
        lexer->lookahead == '<' ||
        lexer->lookahead == ')' ||
        lexer->lookahead == '(' ||
        lexer->lookahead == '[' ||
        lexer->lookahead == ']' ||
        lexer->lookahead == '}' ||
        lexer->lookahead == ';' ||
        lexer->lookahead == '&' ||
        lexer->lookahead == '`'
      )) {
        lexer->result_symbol = CONCAT;
        return true;
      }
    }

    if (valid_symbols[EMPTY_VALUE]) {
      if (iswspace(lexer->lookahead)) {
        lexer->result_symbol = EMPTY_VALUE;
        return true;
      }
    }

    if (valid_symbols[HEREDOC_MIDDLE] && !heredoc_delimiter.empty()) {
      return scan_heredoc_content(lexer, HEREDOC_MIDDLE, HEREDOC_END);
    }

    if (valid_symbols[HEREDOC_BEGINNING]) {
      heredoc_delimiter.clear();
      while (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
        heredoc_delimiter += lexer->lookahead;
        advance(lexer);
      }

      if (lexer->lookahead != '\n') return false;
      advance(lexer);

      if (scan_heredoc_end_identifier(lexer)) {
        lexer->result_symbol = SIMPLE_HEREDOC;
        return true;
      }

      return scan_heredoc_content(lexer, HEREDOC_BEGINNING, SIMPLE_HEREDOC);
    }

    if (valid_symbols[VARIABLE_NAME] || valid_symbols[FILE_DESCRIPTOR]) {
      for (;;) {
        if (
          lexer->lookahead == ' ' ||
          lexer->lookahead == '\t' ||
          (lexer->lookahead == '\n' && !valid_symbols[NEWLINE])
        ) {
          skip(lexer);
        } else if (lexer->lookahead == '\\') {
          skip(lexer);
          if (lexer->lookahead == '\n') {
            skip(lexer);
          } else {
            return false;
          }
        } else {
          break;
        }
      }

      bool is_number = true;
      if (iswdigit(lexer->lookahead)) {
        advance(lexer);
      } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
        is_number = false;
        advance(lexer);
      } else {
        return false;
      }

      for (;;) {
        if (iswdigit(lexer->lookahead)) {
          advance(lexer);
        } else if (iswalpha(lexer->lookahead) || lexer->lookahead == '_') {
          is_number = false;
          advance(lexer);
        } else {
          break;
        }
      }

      if (is_number && valid_symbols[FILE_DESCRIPTOR] && (lexer->lookahead == '>' || lexer->lookahead == '<')) {
        lexer->result_symbol = FILE_DESCRIPTOR;
        return true;
      }

      if (valid_symbols[VARIABLE_NAME] && (lexer->lookahead == '=' || lexer->lookahead == '[')) {
        lexer->result_symbol = VARIABLE_NAME;
        return true;
      }

      return false;
    }

    return false;
  }

  string heredoc_delimiter;
  string current_leading_word;
};

}

extern "C" {

void *tree_sitter_bash_external_scanner_create() {
  return new Scanner();
}

bool tree_sitter_bash_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  return scanner->scan(lexer, valid_symbols);
}

void tree_sitter_bash_external_scanner_reset(void *payload) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  scanner->reset();
}

unsigned tree_sitter_bash_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  return scanner->serialize(buffer);
}

void tree_sitter_bash_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  scanner->deserialize(buffer, length);
}

void tree_sitter_bash_external_scanner_destroy(void *payload) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  delete scanner;
}

}
