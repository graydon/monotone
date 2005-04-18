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

(defvar monotone-password-remember nil
  "*Should Emacs remember your password?
This is a security risk as it could be extracted from memory or core dumps.")

(defvar monotone-password-list nil
  "*The password to be used when monotone asks for one.
List of of (pubkey_id . password ).
If monotone-password-remember is t it will be remembered here.")

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

(defvar monotone-commit-arg nil)
(defvar monotone-commit-dir nil)

(defvar monotone-MT-top nil
  "The directory which contains the MT directory.
This is used to pass state -- best be left nil.")


;;; monotone-commit-mode is used when editing the commit message.
(defvar monotone-commit-mode nil)
(make-variable-buffer-local 'monotone-commit-mode)
(add-to-list 'minor-mode-alist '(monotone-commit-mode " Monotone Commit"))

(defvar monotone-commit-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map "\C-c\C-c" 'monotone-commit-it)
    map))

(defvar monotone-output-mode-hook nil
  "*The hook for monotone output.")

(defvar monotone-commit-instructions
  "MT: --------------------------------------------------
MT: Enter Log.  Lines beginning with `MT:' are removed automatically.
MT: Type C-c C-c to commit, kill the buffer to abort.
MT: --------------------------------------------------"
  "Instructional text to insert into the commit buffer.")

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
    (define-key map "=" 'monotone-vc-diff)
    (define-key map "l" 'monotone-vc-print-log)
    (define-key map "i" 'monotone-vc-register)
    (define-key map "p" 'monotone-vc-pull)
    (define-key map "P" 'monotone-vc-push)
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
  (when (null path)
    (setq path default-directory))
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
  (text-mode)
  (run-hooks monotone-output-mode-hooks))

;; Run a monotone command which does not require IO. (ie: a passwd)
(defun monotone-cmd (&rest args)
  "Execute the monotone command with ARGS in the monotone top directory."
  (let ((mt-top monotone-MT-top))
    (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
      (setq mt-top (monotone-find-MT-top))
      (when (or (not (stringp mt-top)) (not (file-directory-p mt-top)))
        (error "monotone-MT-top is not a directory.")))
    (let ((buf (get-buffer-create monotone-buffer))
          (cmd-args (append (list monotone-program) args)))
      (switch-to-buffer-other-window buf)
      (toggle-read-only -1)
      (erase-buffer)
      (cd mt-top)
      (shell-command (mapconcat 'identity cmd-args " ") buf)
      (goto-char (point-min))
      ;; this should be monotone-output-mode
      (view-mode))))

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

(defun monotone-server-prompt ()
  (let ((svr (or monotone-server "")))
    (setq monotone-server
          (read-from-minibuffer 
           "Server (address[:port])? " nil
           'monotone-server-hist svr 

  (when (string= monotone-server "nil")
    (setq monotone-server nil)))
;; (monotone-server-prompt)

(read-string nil (format "foo"))


;;(defun monotone-pull (&optional arg)
;;  "Pull updates from a remote server, prompt 
;;Prompt for acollection"
;;  (interactive "P")
;;  (if 
;;  (let ((cmd (list "--ticker=dot" "pull")))
;;    ;; given address?
;;    (when (and (stringp monotone-pull-address)
;;               (not (string= monotone-pull-address "")))
;;      (setq cmd (append cmd (list monotone-pull-address)))
;;      ;; given collection?
;;      (when (and (stringp monotone-collection)
;;                 (not (string= monotone-collection "")))
;;        (setq cmd (append cmd (list monotone-collection)))))
;;    ;;
;;    (apply 'monotone-cmd cmd)))


;;;;

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
  (monotone-cmd-buf arg (current-buffer) "diff")
  (diff-mode))

(defun monotone-vc-register ()
  "Register this file with monotone for the next commit."
  (interactive)
  (monotone-cmd-buf nil (current-buffer) "add"))

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
