;;; monotone.el --- Run monotone from within Emacs.
;;
;; Copyright 2005 by Olivier Andrieu <oandrieu@nerim.net>
;; Version 0.2 2005-04-13
;;
;; Licence: GPL v2 (same as monotone)
;; Keymaps and other stuff added by harley@panix.com

;;; Commentary:
;;
;; This defines `monotone-diff', `monotone-status', `monotone-add',
;; `monotone-drop', `monotone-revert' and `monotone-commit'.  These
;; functions call the corresponding monotone command, restricted to
;; the current file.  With a prefix argument (C-u) the command is
;; applied unrestricted (on the whole tree).  As an exception,
;; `monotone-status' has the opposite behaviour: it is unrestricted by
;; default, restricted with a prefix argument.
;;
;; /!\ beware of bugs: `monotone-commit' is more dangerous than the
;; others since it writes to the database.
;; 
;; To use monotone from within Emacs, decide what options you would
;; like and set the vars before loading monotone.el
;;
;;   (require 'monotone)
;;

;;; User vars:
;; These vars are likley to be changed by the user.

(defvar monotone-program "monotone"
  "*The path to the monotone program.")

(defvar monotone-passwd-remember nil
  "*Should Emacs remember your monotone passwords?
This is a security risk as it could be extracted from memory or core dumps.")

(defvar monotone-passwd-alist nil
  "*The password to be used when monotone asks for one.
List of of (pubkey_id . password ).
If monotone-passwd-remember is t it will be remembered here.")

;; This is set to [f5] for testing.
;; Should be nil for general release, as we dont want to
;; remove keys without the users consent.
(defvar monotone-vc-key  [f5] ;; "\C-xv" nil
  "The prefix key to use for the monotone vc key map.
You may wish to change this before loading monotone.el.
Habitual monotone users can set it to '\C-xv'.")


;;; System Vars:
;; It is unlikely for users to change these.

(defvar monotone-buffer "*monotone*"
  "The buffer used for displaying monotone output.")

(defvar monotone-commit-buffer "*monotone commit*"
  "The name of the buffer for the commit message.")
(defvar monotone-commit-edit-status nil
  "The sentinel for completion of editing the log.")
(make-variable-buffer-local 'monotone-commit-edit-status)
(defvar monotone-commit-args nil
  "The args for the commit.")
(make-variable-buffer-local 'monotone-commit-args)

(defvar monotone-commit-dir nil)

(defvar monotone-MT-top nil
  "The directory which contains the MT directory.
This is used to pass state -- best be left nil.")


;;; monotone-commit-mode is used when editing the commit message.
(defvar monotone-commit-mode nil)
(make-variable-buffer-local 'monotone-commit-mode)

(defvar monotone-commit-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-c" 'monotone-commit-complete)
    map))

;; hook it in
(add-to-list 'minor-mode-alist '(monotone-commit-mode " Monotone Commit"))
(add-to-list 'minor-mode-map-alist (cons 'monotone-commit-mode monotone-commit-mode-map))

(defvar monotone-msg nil
  "When non-nil log stuff to *Messages*")

(defmacro monotone-msg (&rest args)
  `(when monotone-msg
     (message ,@args)))
;; (monotone-msg "%s" '("foo" 1 2 3))

(defvar monotone-output-mode-hook nil
  "*The hook for monotone output.")

(defvar monotone-commit-instructions
  "--------------------------------------------------
Enter Log.  Lines beginning with 'MT:' are removed automatically.
Type C-c C-c to commit, kill the buffer to abort.
--------------------------------------------------"
  "Instructional text to insert into the commit buffer. 
'MT: ' is added when inserted.")

(defvar monotone-commit-mode-hook nil
  "*The hook for monotone-commit-mode.")


(defvar monotone-server nil
  "The default server for pulls and pushes.")
(defvar monotone-collection nil
  "The default collection for pulls and pushes.")
(defvar monotone-server-hist nil
  "A history of servers.")
(defvar monotone-collection-hist nil
  "A history of collections.")

;;; Key maps
(defvar monotone-vc-map
  (let ((map (make-sparse-keymap)))
    (define-key map "="    'monotone-vc-diff)
    (define-key map "P"    'monotone-vc-push)
    (define-key map "\C-q" 'monotone-vc-commit)
    (define-key map "i"    'monotone-vc-register)
    (define-key map "i"    'monotone-vc-register)
    (define-key map "l"    'monotone-vc-print-log)
    (define-key map "p"    'monotone-vc-pull)
    (define-key map "q"    'monotone-vc-commit) ;; i am a lazy typist
    (define-key map "s"    'monotone-vc-status)
    map))
(fset 'monotone-vc-map monotone-vc-map)

;;; Code:

;; install the keymaps
(when monotone-vc-key
  (define-key global-map monotone-vc-key 'monotone-vc-map))

(defun monotone-toggle-vc-map (&optional arg)
  "Toggle between the default and monotone vc-maps, ARG set map.
With arg 0 use the default variable `vc-prefix-map'.
With t use monotone-vc-prefix-map."
  (interactive "P")
  (message "Arg: %s" arg)
  (define-key ctl-x-map "v"
    (let ((current (lookup-key ctl-x-map "v")))
      (if (and (not (eq arg 0))
               (or arg (not (eq current monotone-vc-map))))
        monotone-vc-prefix-map
        vc-prefix-map))))
;; (monotone-toggle-vc-map t)

;; Utility functions
(defun monotone-file-parent-directory (file)
  "Return the parent directory of FILE."
  (file-name-directory (directory-file-name file)))

(defun monotone-find-MT-top (&optional path)
  "Find the directory which contains the 'MT' directory.
Optional argument PATH ."
  (setq path (or path (buffer-file-name) default-directory))
  (when (null path)
    (error "Cant find top for %s" path))
  ;; work with full path names
  (setq path (expand-file-name path))
  (block nil
    (let ((prev-path nil))
      (while (not (equal path prev-path))
        (let ((mt-dir (concat path "MT")))
          ;;(message "Search: %s" mt-dir)
          (when (file-directory-p mt-dir)
            (return path))
          (setq prev-path path
                path (monotone-file-parent-directory path)))))))
;;(monotone-find-MT-top "/disk/amelie1/harley/monotone-dev/contrib/monotone.el")

(defun monotone-extract-MT-path (path &optional mt-top)
  "Get the PATH minus the MT-TOP."
  (let ((mt-top (or mt-top monotone-MT-top (monotone-find-MT-top path))))
    ;; work with full names
    (setq path (expand-file-name path)
          mt-top (expand-file-name mt-top))
    ;;
    (if (not mt-top)
      nil
      (substring path (length mt-top)))))
;;(monotone-extract-MT-path "/disk/amelie1/harley/monotone-dev/contrib/monotone.el")
;;(monotone-find-MT-dir "/disk/amelie1/harley")

;;
(defun monotone-output-mode ()
  "In the future this will provide some fontification.
Nothing for now."
  (interactive)
  (fundamental-mode) ;;(text-mode)
  (run-hooks monotone-output-mode-hooks))

;;(define-derived-mode monotone-shell-mode comint-mode "Monotone")

;; Run a monotone command
(defun monotone-cmd (&rest args)
  "Execute the monotone command with ARGS in the monotone top directory."
  (monotone-msg "%s" args)
  (let ((mt-top (or monotone-MT-top (monotone-find-MT-top)))
        (mt-buf (get-buffer-create monotone-buffer))
        ;;(mt-pgm "ls") ;; easy debugging
        (mt-pgm monotone-program)
        monotone-MT-top
        mt-cmd mt-status)
    ;; where to run
    (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
      (setq mt-top (monotone-find-MT-top))
      (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
        (error "Unable to find the MT top directory")))
    (setq monotone-MT-top mt-top)
    ;; show the window
    (if (not (equal (current-buffer) mt-buf))
      (switch-to-buffer-other-window mt-buf))
    ;; still going?
    (if (get-buffer-process mt-buf)
      (error "Monotone is currently running"))
    ;; prep the buffer for output
    (toggle-read-only -1)
    (erase-buffer)
    (buffer-disable-undo (current-buffer))
    (setq default-directory mt-top)
    ;; run
    (let ((p (apply #'start-process monotone-buffer mt-buf mt-pgm args)))
      (while (eq (process-status p) 'run)
        (accept-process-output p)
        (goto-char (point-max))
        ;; look for passwd prompt
        (beginning-of-line)
        (when (looking-at "^enter passphrase for key ID \\(.*\\)")
          (let ((pass (monotone-read-passwd (match-string 1))))
            (insert "********\n") ;; 
            (process-send-string p (concat pass "\n")))))
      (setq mt-status (process-exit-status p)))
    ;; make the buffer nice.
    (goto-char (point-min))
    (view-mode)
    ;; did we part on good terms?
    (if (not (zerop mt-status))
      (error (format "%s: exited with status %s" mt-pgm mt-status)))
    mt-status))

(defun monotone-cmd-hide (&rest args)
  "Run the command without showing the output."
  (save-window-excursion
    (apply #'monotone-cmd args)))
  

;; (monotone-cmd "list" "branches")
;; (monotone-cmd "list" "keys")
;; (monotone-cmd  "pubkey" "harley@panix.com")
;; (let ((monotone-cmd-hide t)) (monotone-cmd "status"))

(defun monotone-read-passwd (keypairid)
  (let ((rec (assoc keypairid monotone-passwd-alist))
        prompt passwd)
    (setq prompt (format "Password for '%s'%s: " keypairid 
                         (if rec " [return for default]" "")))
    (setq passwd (read-passwd prompt nil (cdr rec)))
    (when monotone-passwd-remember
      (if rec 
        (setcdr rec passwd)
        (setq monotone-passwd-alist 
              (cons (cons keypairid passwd) monotone-passwd-alist))))
    passwd))
;; (monotone-read-passwd "bar1")
;; (setq monotone-passwd-remember t)
;; monotone-passwd-alist

;; a simple catch all
(defun monotone-do (string)
  "Prompt for argument STRING to run monotone with.  Display output.
The monotone command is expected to run without input."
  (interactive "sMonotone: ")
  (monotone-cmd string))

;;
(defun monotone-list-branches ()
  "List the monotone branches known."
  (interactive)
  (monotone-cmd "list" "branches"))

(defun monotone-pull (&optional server collection)
  ;;(interactive "sServer: \nsCollection: \n")
  (let ((cmd (list "--ticker=dot" "pull"))
        (svr (or server monotone-server ""))
        (col (or collection monotone-collection "")))
    ;; given address?
    (when (and (stringp svr) (not (string= svr "")))
      (setq cmd (append cmd (list svr)))
      ;; given collection?
      (when (and (stringp col) (not (string= col "")))
        (setq cmd (append cmd (list col)))))
    ;;
    (apply 'monotone-cmd cmd)))
;; (monotone-pull)

(defun monotone-vc-pull ()
  "Pull updates from a remote server. With ARG prompt for server and collection.
With an arg of 0, clear default server and collection."
  (interactive)
  ;; read-string docs say not to use initial-input but "compile" does.
  (setq monotone-server 
        (read-string "Monotone server: " monotone-server
                     'monotone-server-hist))
  (setq monotone-collection
        (read-string "Monotone collection: " monotone-collection
                     'monotone-collection-hist))
  (monotone-pull monotone-server monotone-collection))
;; (monotone-vc-pull)

;;; Commiting...

(defun monotone-vc-commit (&rest args)
  "Commit the current buffer.  With ARG do a global commit."
  (interactive "P")
  (let ((buf (get-buffer-create monotone-commit-buffer))
        (monotone-MT-top (monotone-find-MT-top)))
    ;; found MT?
    (when (not monotone-MT-top)
      (error "Cant find MT directory"))
    ;; show it  
    (when (not (equal (current-buffer) buf))
      (switch-to-buffer-other-window buf))
    (set-buffer buf)
    ;; Have the contents been commited?
    (when (eq monotone-commit-edit-status 'started)
      (message "Continuing commit message already started."))
    (when (or (null monotone-commit-edit-status) (eq monotone-commit-edit-status 'done))
      (erase-buffer)
      (setq default-directory monotone-MT-top)
      (let ((mt-log-path (concat monotone-MT-top "MT/log")))
        (when (file-readable-p mt-log-path)
          (insert-file mt-log-path)))
      ;; blank line for message.
      (beginning-of-buffer)
      (insert "\n")
      (beginning-of-buffer)
      (monotone-commit-mode))
    ;; update the "MT:" lines by replacing them.
    (monotone-remove-MT-lines)
    (end-of-buffer)
    (when (not (looking-at "^"))
      (insert "\n"))
    (let ((eo-message (point)))
      ;; what is being commited?
      (mapc (function (lambda (a) (insert "args: " (format "%s" a) "\n"))) args)
      ;;(insert (format "Commit arg = %s" arg) "\n")
      ;; instructional text
      (when (stringp monotone-commit-instructions)
        (insert monotone-commit-instructions)
        (when (not (looking-at "^"))
          (insert "\n")))
      ;; what is being committed?
      ;; FIXME: handle args -- this is doing a global status
      (monotone-cmd-hide "status")
      (insert-buffer-substring monotone-buffer)
      ;; insert "MT: " prefix
      (goto-char eo-message)
      (while (search-forward-regexp "^" (point-max) t)
        (insert "MT: ")))
    ;; ready for edit -- put this last avoid being cleared on mode switch.
    (beginning-of-buffer)
    (setq monotone-commit-edit-status 'started
          monotone-commit-args args)))

(defun monotone-commit-mode ()
  "Mode for editing a monotone commit message."
  ;; turn on the minor mode for keybindings and run hooks.
  (fundamental-mode) ;; (text-mode)
  (setq monotone-commit-mode t)
  (run-hooks monotone-commit-mode-hook))

(defun monotone-commit-complete ()
  (interactive)
  (monotone-remove-MT-lines)
  (let ((buf (current-buffer))
        (message (buffer-substring-no-properties (point-min) (point-max)))
        (args (list "commit")))
    (switch-to-buffer (get-buffer-create monotone-buffer))
    ;; assemble and run the command
    (setq args (append args (list  "--message" message)))
    ;; FIXME: global subtree file list...
    (when monotone-commit-args
      (setq args (append args (list "."))))
    (apply #'monotone-cmd args)
    ;; mark it done
    (set-buffer buf)
    (setq monotone-commit-edit-status 'done)))
  

(defun monotone-remove-MT-lines ()
  (interactive)
  (beginning-of-buffer)
  (while (search-forward-regexp "^MT:.*$" (point-max) t)
    (beginning-of-line)
    (kill-line 1)))
  
;; check for common errors and args.
(defun monotone-cmd-buf (global buf cmd)
  "Run a simple monotone command for this buffer.  (passwordless)
GLOBAL runs the command without the buffer.
BUF is the buffer if not global.
CMD is the command to execute."
  (let ((bfn (buffer-file-name)))
    (when (not bfn)
      (error "No file-name for buffer"))
    (let* ((monotone-MT-top (monotone-find-MT-top bfn))
           (bmn (monotone-extract-MT-path bfn)))
      (if global
        (monotone-cmd cmd)
        (monotone-cmd cmd bmn)))))

;; NOTE: The command names are modeled after the vc command names.

(defun monotone-vc-print-log (&optional arg)
  "Print the log for this buffer.  With prefix ARG the global log."
  (interactive "P")
  (monotone-cmd-buf arg (current-buffer) "log"))

;; (monotone-print-log)

(defun monotone-vc-diff (&optional arg)
  "Print the diffs for this buffer.  With prefix ARG, the global diffs."
  (interactive "P")
  (let ((mt-top (monotone-find-MT-top))
        (bfn (buffer-file-name)))
    (monotone-cmd "diff"
                  (if bfn
                    (monotone-extract-MT-path bfn mt-top)
                    "."))
    (diff-mode)))

(defun monotone-vc-register ()
  "Register this file with monotone for the next commit."
  (interactive)
  (if buffer-file-name
    (monotone-cmd-buf nil (current-buffer) "add")
    (error "This buffer does not have a file name")))

(defun monotone-vc-status ()
  (interactive)
  (monotone-cmd "status"))

(defun monotone-vc-update-change-log ()
  "Edit the monotone change log."
  (interactive)
  (let ((mt-top (monotone-find-MT-top)))
    (when (not mt-top)
      (error "Unable to find MT directory"))
    (find-file-other-window (concat mt-top "MT/log"))))

;; (monotone-vc-update-change-log)

(provide 'monotone)
;;; monotone.el ends here
