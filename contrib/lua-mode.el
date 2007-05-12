;; Major mode for editing Lua files.
;; For info on Lua, see http://www.lua.org/
;;
;; Copyright (C) 2007 Stephen Leake
;;
;; Author   : Stephen Leake <stephen.leake@gsfc.nasa.gov>
;; Web Site : http://www.stephe-leake.org/
;;
;; Keywords: script, lua
;;
;; lua-mode requires GNU Emacs 22.1 or newer
;;
;; This file is NOT part of GNU Emacs, but is distributed under
;; the GNU General Public License as published by the Free Software
;; Foundation; either version 2, or (at your option) any later
;; version.

;; This code is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs; see the file COPYING.  If not, write to the
;; Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.

;;; USAGE
;;; =====
;;; The main starting point is lua-mode; it sets up fontification and
;;; comment syntax.

;;;--------------------
;;;    USER OPTIONS
;;;--------------------

(defgroup lua nil
  "Major mode for editing LUA script files"
  :group 'languages)

(defcustom lua-mode-hook nil
  "*List of functions to call when lua mode is invoked."
  :type 'hook
  :group 'lua)

;;; ---- end of user configurable variables

(defvar lua-mode-map (make-sparse-keymap)
  "Local keymap for lua mode.")

(require 'align)

(defvar lua-align-modes
  '((lua-assignment
     (regexp  . "[^=]\\(\\s-+\\)=[^=]")
     (valid   . (lambda() (not (lua-in-comment-p))))
     (modes   . '(lua-mode)))
    (lua-comment
     (regexp  . "--")
     (modes   . '(lua-mode)))
    )
  "lua support for align.el >= 2.8.")

;; main entry point
;;;###autoload
(defun lua-mode ()
  "lua mode is a very simple major mode for editing Lua script files.
Keybindings:
\\{lua-mode-map}"
  (interactive)
  (kill-all-local-variables)
  (set (make-local-variable 'comment-start) "-- ")
  (set (make-local-variable 'comment-start-skip) "-- ")
  (set (make-local-variable 'font-lock-defaults)
       '((lua-font-lock-keywords) ; keywords
         nil                      ; keywords-only
         t                        ; case-fold-font
         nil                      ; syntax-alist
         nil))                    ; syntax-begin

  (setq major-mode 'lua-mode)
  (setq mode-name "Lua")
  (use-local-map lua-mode-map)
  (set-syntax-table lua-mode-syntax-table)

  (add-to-list 'align-open-comment-modes 'lua-mode)
  (set (make-variable-buffer-local 'align-region-separate)
       'group)
  (setq align-mode-rules-list lua-align-modes)

  (run-hooks 'lua-mode-hook) )

;;; general support

;; syntax
(defvar lua-mode-syntax-table (make-syntax-table)
  "Syntax table for editing Lua script files.")

(modify-syntax-entry ?\_  "w" lua-mode-syntax-table) ; underscore is a word constituent
(modify-syntax-entry ?\-  ". 12" lua-mode-syntax-table) ; comment start
(modify-syntax-entry ?\n  ">   " lua-mode-syntax-table) ; comment end

;; font lock
(defconst lua-identifier-regexp "\\([A-Za-z0-9_]+\\)"
  "Regexp for extracting lua identifiers.")

(defconst lua-file-regexp "\\([A-Za-z0-9_./:]+\\)"
  "Regexp for extracting lua identifiers.")

(defconst lua-string-simple-keywords
  '("and" "break" "do" "else" "elseif" "end" "false" "for" "function" "if"
    "in" "local" "nil" "not" "or" "repeat" "return" "then" "true" "until" "while")
  "All Lua keywords that don't need special treatment in font-lock")

(defconst lua-simple-keywords
  (concat "\\<" (regexp-opt lua-string-simple-keywords t) "\\>")
  "Optimized regexp for simple Lua keywords.")

(defconst lua-font-lock-keywords
  (list
   (list lua-simple-keywords
         '(1 font-lock-keyword-face))
   )
   "highlighting for lua mode")

(defsubst lua-in-comment-p (&optional parse-result)
  "Returns t if inside a comment."
  (nth 4 (or parse-result
             (parse-partial-sexp
              (line-beginning-position) (point)))))

;;; pages
(defconst lua-page-marker "----------" ; exactly 10 dashes
  "Lua page delimiter.")

(defun lua-prev-page ()
  "Move to previous page boundary; `lua-page-marker' or beginning of buffer."
  (interactive)
    (if (re-search-backward lua-page-marker (point-min) t)
      (goto-char (match-beginning 0))
    (goto-char (point-min))))

(defun lua-next-page ()
  "Move to next page boundary; `lua-page-marker' or end of buffer."
  (interactive)
  (end-of-line)
  (if (re-search-forward lua-page-marker (point-max) t)
      (goto-char (match-beginning 0))
    (goto-char (point-max))))

(define-key lua-mode-map [prior] 'lua-prev-page)
(define-key lua-mode-map [next] 'lua-next-page)

(provide 'lua-mode)
;;; end of file
