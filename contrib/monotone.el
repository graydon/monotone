;;; monotone.el --- Run monotone from within Emacs.
;; Copyright 2005 by Olivier Andrieu <oandrieu@nerim.net>

;; This defines `monotone-diff', `monotone-status', `monotone-add',
;; `monotone-drop', `monotone-revert' and `monotone-commit'. They call
;; monotone on the current file. With a prefix argument (C-u) the
;; command is applied on the whole tree.

;; /!\ beware of bugs: `monotone-commit' is more dangerous than the
;; other two as it modifies the database.


(defvar monotone-program
  "monotone"   "The path of the monotone program")
(defvar monotone-buffer 
  "*monotone*" "The buffer used for displaying monotone output")

(defvar monotone-map nil)
(defvar monotone-commit-arg nil)
(defvar monotone-commit-dir nil)

(defun may-append (arg &rest other-args)
  (if arg
      (append other-args (list arg))
    other-args))

(defun monotone-run (command &optional arg)
  (unless buffer-file-name
    (error "This buffer is not associated with a file"))
  (let ((file
	 (if arg 
	     nil
	   (file-name-nondirectory buffer-file-name)))
	(dir (file-name-directory buffer-file-name)))
    (pop-to-buffer monotone-buffer)
    (setq buffer-read-only nil)
    (erase-buffer)
    (cd dir)
    (apply 'call-process monotone-program nil t nil 
	   (may-append file command))
    (goto-char (point-min))
    (fundamental-mode)
    (setq buffer-read-only t)
    (if (zerop (buffer-size))
	(delete-window)
      (shrink-window-if-larger-than-buffer))))

(defun monotone-diff (arg)
  "Run `monotone diff' on the current buffer's file. When called with
a prefix argument, do it for the whole tree."
  (interactive "P")
  (save-selected-window
    (monotone-run "diff" arg)
    (diff-mode)))

(defun monotone-status (arg)
  "Run `monotone status' on the current buffer's file. When called with
a prefix argument, do it for the whole tree."
  (interactive "P")
  (save-selected-window
    (monotone-run "status" arg)))

(defun monotone-add ()
  "Run `monotone add' on the current buffer's file."
  (interactive)
  (save-selected-window
    (monotone-run "add")))

(defun monotone-drop ()
  "Run `monotone drop' on the current buffer's file."
  (interactive)
  (save-selected-window
    (monotone-run "drop")))

(defun monotone-revert ()
  "Run `monotone revert' on the current buffer's file."
  (interactive)
  (if (and buffer-file-name
	   (yes-or-no-p 
	    (format "Are you sure you want monotone to revert '%s' ? " buffer-file-name)))
      (save-selected-window
	(monotone-run "revert")
	(revert-buffer t t))))

(defun monotone-install-keymap ()
  (unless monotone-map
    (let ((km (make-sparse-keymap)))
      (set-keymap-parent km text-mode-map)
      (define-key km "\C-c\C-c" 'monotone-commit-do)
      (setq monotone-map km)))
  (use-local-map monotone-map))

(defun monotone-commit-do ()
  (interactive)
  (let (log-message p)
    (set-buffer "*monotone ChangeLog*")
    (goto-char (point-min))
    (while (re-search-forward "^MT: [^\n]*\n?" nil t)
      (replace-match ""))
    (setq log-message (buffer-string))
    (switch-to-buffer monotone-buffer)
    (fundamental-mode)
    (setq buffer-read-only nil)
    (erase-buffer)
    (cd monotone-commit-dir)
    (setq p 
	  (apply 'start-process "monotone" monotone-buffer monotone-program
		 (may-append monotone-commit-arg
			     "--message" log-message "commit")))

    (while (eq (process-status p) 'run)
      (accept-process-output p)
      (goto-char (point-max))
      (forward-line 0)
      (when (looking-at "^enter passphrase for key ID \\(.*\\)")
	(let ((pass (read-passwd (concat "Passphrase " (match-string 1)))))
	  (process-send-string p (concat pass "\n")))))
    (if (not (zerop (process-exit-status p)))
	(error "Monotone commit exited abnormally"))))

(defun monotone-commit (arg)
  "Run `monotone commit' on the current buffer's file. When called
with a prefix argument, do it on the whole tree."
  (interactive "P")
  (if (null buffer-file-name)
      (error "This buffer is not associated with a file"))
  (let ((file
	 (if arg 
	     nil
	   (file-name-nondirectory buffer-file-name)))
	(dir (file-name-directory buffer-file-name)))

    (pop-to-buffer "*monotone ChangeLog*")
    (setq monotone-commit-arg file
	  monotone-commit-dir dir)
    (setq buffer-read-only nil)
    (erase-buffer)
    (cd dir)
    (apply 'call-process monotone-program nil t nil 
	   (may-append file "status"))
    (goto-char (point-min))
    (while (progn
	     (insert "MT: ")
	     (= 0 (forward-line 1))))
    (goto-char (point-min))
    (dolist 
	(l '(""
	     "MT: ----------------------------------------------------------------------"
	     "MT: Enter Log.  Lines beginning with `MT:' are removed automatically."
	     "MT: Type C-c C-c to commit, kill the buffer to abort."))
      (insert l "\n"))
    (goto-char (point-max))
    (insert "----------------------------------------------------------------------\n")
    (text-mode)
    (monotone-install-keymap)
    (goto-char (point-min))
    (shrink-window-if-larger-than-buffer)))
