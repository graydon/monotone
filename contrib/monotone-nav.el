;;; monotone-nav.el --- A navigator of monotone revision histories.
;;
;; ~/share/emacs/pkg/monotone/monotone-db.el ---
;;
;; $Id: monotone-nav.el,v 1.18 2005/04/26 07:42:22 harley Exp $
;;

;;; Commentary:
;; "monotone-nav" is a database browser for monotone databases.
;; Using the sql option of monotone it extacts the data into
;; emacs which you may browse using the arrow keys.
;; You can also mark revisons to run diffs or do other work.

;;; EXAMPLE USAGE:
;; (mnav-revdb-reload)
;; (message "mnav-pick: %s" (mnav-rev-id (mnav-pick)))

(require 'base64)
(require 'cl)
(require 'monotone)

;;; Code:
(defvar mnav-rev-point nil
  "The rev record at 'point' of the picker.  Get its id with 'mnav-rev-id'.")
(defvar mnav-rev-mark nil
  "The rev record 'marked' by the picker.  Get its id with 'mnav-rev-id'.")

;;
(defvar mnav-pick-min nil
  "The min value allowed to pick.")
(defvar mnav-pick-cur nil
  "The current selection on the pick screen.")
(defvar mnav-pick-max nil
  "The max value allowed to pick.")
(defvar mnav-pick-point nil
  "The point location for the cursor.")

;;
(defvar mnav-revdb nil
  "A hash maping revision ids to rev structs.")
(defvar mnav-revdb-initsize 5000
  "The initial size of the hashtable.")

;; debug info
(defvar mnav-run-query-rows nil)

;; many of the keys are repeats, so generate them.
(defvar mnav-pick-readkey-map
  (let ((map (make-sparse-keymap))
        (acts '((quit      "\C-g" "q" "Q" "x" "X")
                (pick      "\C-M" "p" "P")
                (select    [right] " ")
                (mark      "m" "M")
                (unmark    "u" "U")
                ;;(head      "h" "H")
                (swap      "s" "S")
                (move-up   [up] "-")
                (move-down [down] "=" "+")
                (back      [left] "l" "L" "b" "B") )))
    (dolist (act acts)
      (dolist (key (cdr act))
        (define-key map key (car act))))
    (dotimes (i 10)
      (define-key map (format "%s" i) i))
    ;; normal key defs
    (define-key map "d" 'mnav-diff-revisions1)
    (define-key map "D" 'mnav-diff-revisions2)
    (define-key map "f" 'mnav-diff-file)
    map)
  "The keymap used by `mnav-pick-readkey'.
The values are either symbols for `mnav-pick' actions or
interactive functions which will be exectued in the context of the picker.
Users can use `define-key' to modifiy the mappings.")

;; (mnav-pick-readkey)
;; (read-key-sequence "Key: ")

;;;;;;;;;;

;;; the Revision structure
(defmacro mnav-rev-id (rec)
  "Fetch id from REC."
  `(nth 1 ,rec))
(defmacro mnav-rev-author (rec)
  "Fetch the author from REC."
  `(nth 2 ,rec))
(defmacro mnav-rev-date (rec)
  "Fetch the date from REC."
  `(nth 3 ,rec))
(defmacro mnav-rev-parents (rec)
  "Fetch the list of parent revisions from REC."
  `(nth 4 ,rec))
(defmacro mnav-rev-children (rec)
  "Fetch the list of child revisions from REC."
  `(nth 5 ,rec))
(defmacro mnav-rev-changelog (rec)
  "Fetch the changelog from REC."
  `(nth 6 ,rec))
(defmacro mnav-rev-branch (rec)
  "Fetch the name of the branch from REC."
  `(nth 7 ,rec))
(defmacro mnav-rev-tag (rec)
  "Fetch the tags from REC."
  `(nth 8 ,rec))
(defmacro mnav-rev-pick-back (rec) ;; the picker stores the "back link" here
  "Fetch the prior record viewed from REC.
This is not from the DB but used by mnav-pick."
  `(nth 9 ,rec))
(defmacro mnav-rev-pick-cur (rec) ;; the picker stores mnav-pick-cur here.
  "Fetch the current link selected from REC.
This is not from the DB but used by mnav-pick."
  `(nth 10 ,rec))
(defun mnav-rev-make (id)
  "Create a mnav-rev structure. ID is required."
  (let ((rec (make-list 11 nil)))
    (setf (car rec) 'rev)
    (setf (mnav-rev-id rec) id)
    rec))
(defun mnav-rev-p (rec)
  "Is this an mnav-rec?"
  (and (listp rec) (equal (car rec) 'rev)))
;; (mapcar #'mnav-rev-p (list nil (mnav-rev-make "aaa")))

(defun mnav-rev-string (rec)
  "Cast the revison record REC to a string.
This is used for debugging."
  (format
   "#<rev %s p=%d c=%d %s %s>"
   (or (mnav-rev-id rec) (make-string 40 63))
   (length (mnav-rev-parents rec))
   (length (mnav-rev-children rec))
   (or (mnav-rev-date   rec) "???")
   (or (mnav-rev-author rec) "???")))
;; (mnav-rev-string nil)

(defun mnav-rev-nth-link (n rev)
  "Return link N from the revision REV.
Links are numbered in order starting with the parent."
  (let ((plen (length (mnav-rev-parents  rev)))
        (clen (length (mnav-rev-children rev))))
    (assert (< n (+ plen clen)) t "N is out of bounds: %s" n)
    (if (< n plen)
      (nth    n       (mnav-rev-parents  rev))
      (nth (- n plen) (mnav-rev-children rev)))))
;; (mnav-rev-nth-link 0 '(rev 1 2 3 (4 a b c) (5 d e f) 6 7 8))

;;;;;;;;;;

;;; The index to revision entries

(defun mnav-revdb-clear ()
  "Clear the REVDB by creating a new hash table."
  (setq mnav-rev-point nil
        mnav-rev-mark  nil)
  (setq mnav-revdb (make-hash-table :test #'equal :size mnav-revdb-initsize)))
;; (mnav-revdb-clear)

(defun mnav-revdb-find (id &optional create)
  "Find the ID in the revdb. CREATE if t."
  ;; init?
  (when (null mnav-revdb)
    (mnav-revdb-clear))
  (when (not (stringp id))
    (if (mnav-rev-p id)
      (setq id (mnav-rev-id id))
      (error "ID is not a string or REV record.")))
  ;;
  (let ((rev (gethash id mnav-revdb)))
    (when (and (not rev) create)
      (setq rev (mnav-rev-make id))
      (puthash id rev mnav-revdb))
    rev))

(defun mnav-revdb-print ()
  "Dump the contents of revdb to a buffer for debugging."
  (when (not mnav-revdb)
    (mnav-revdb-clear))
  (let ((buf (get-buffer-create "*monotone revdb*")))
    (set-buffer buf)
    (erase-buffer)
    (maphash (function (lambda (k v) (insert (mnav-rev-string v) "\n"))) mnav-revdb)
    (goto-char (point-min))
    (switch-to-buffer-other-window buf)))
;; (mnav-revdb-print)

(defun mnav-revdb-add-ancestry (parentid childid)
  "Add links from PARENTID to CHILDID."
  (let ((p-rec (mnav-revdb-find parentid t))
        (c-rec (mnav-revdb-find childid t)))
    (push c-rec (mnav-rev-children p-rec))
    (push p-rec (mnav-rev-parents  c-rec))
    nil))

;; (mnav-revdb-print)

(defun mnav-query-run (sqlquery row-func)
  (let ((buf (get-buffer monotone-buffer))
        read-mark row)
    (let ((monotone-cmd-show nil)) ;; dont show the output
      (monotone-cmd (list "db" "execute" sqlquery))
      ;; skip to data
      (goto-char (point-min))
      (search-forward-regexp "^$" (point-max) t)
      (setq read-mark (point-marker))
      ;;
      (setq mnav-query-rows nil) ;; debug
      (while (setq row (condition-case nil (read read-mark) (error nil)))
        (funcall row-func row)
        (setq mnav-query-rows (cons row mnav-query-rows)) ;; debug
        nil))))

(defun mnav-revdb-query-ancestry ()
  "Query the revision_ancestry table for ancestry info."
  ;; ("parentid" "childid")
  (mnav-query-run "
select '(\"'||coalesce(parent,'')||'\" \"'||coalesce(child,'')||'\")'
from revision_ancestry"
                  (function (lambda (row) (mnav-revdb-add-ancestry (car row) (cadr row))))))

(defun mnav-revdb-query-metaname (name setfunc)
  "Query for metadata NAME and apply SETFUNC to each row.
SETFUNC is called with the revision rec and *decoded* value."
  (let ((sql (format "
select '(\"'||id||'\" \"'||value||'\")'
from revision_certs
where name = '%s'" name)))
    (mnav-query-run
     sql
     (function
      (lambda (row)
        (let ((rec (mnav-revdb-find (car row) t))
              (val (base64-decode-string (cadr row))))
          (funcall setfunc rec val)))))))

;; the revision info we care about.
(defun mnav-revdb-query-meta-author ()
  (mnav-revdb-query-metaname
   "author"
   (function (lambda (rec val) (setf (mnav-rev-author rec) val)))))
(defun mnav-revdb-query-meta-date ()
  (mnav-revdb-query-metaname
   "date"
   (function (lambda (rec val) (setf (mnav-rev-date rec) val)))))
(defun mnav-revdb-query-meta-changelog ()
  (mnav-revdb-query-metaname
   "changelog"
   (function (lambda (rec val) (setf (mnav-rev-changelog rec) val)))))
(defun mnav-revdb-query-meta-branch ()
  (mnav-revdb-query-metaname
   "branch"
   (function (lambda (rec val) (setf (mnav-rev-branch rec) val)))))
(defun mnav-revdb-query-meta-tag ()
  (mnav-revdb-query-metaname
   "tag"
   (function (lambda (rec val) (setf (mnav-rev-tag rec) val)))))

(defun mnav-revdb-reload ()
  (mnav-revdb-clear)
  (message "Loading ancestry...")
  (mnav-revdb-query-ancestry)
  (message "Loading authors...")
  (mnav-revdb-query-meta-author)
  (message "Loading dates...")
  (mnav-revdb-query-meta-date)
  (message "Loading changelogs...")
  (mnav-revdb-query-meta-changelog)
  (message "Loading branches...")
  (mnav-revdb-query-meta-branch)
  (message "Loading tags...")
  (mnav-revdb-query-meta-tag)
  nil)
;; (mnav-revdb-reload)


;;;;;;;;;;

;;; PICK

(defun mnav-pick-clamp ()
  "Clamp mnav-pick-cur between min and max."
  (when (or (not (numberp mnav-pick-cur)) (< mnav-pick-cur mnav-pick-min))
    (setq mnav-pick-cur mnav-pick-min))
  (when (not (< mnav-pick-cur mnav-pick-max))
    (setq mnav-pick-cur (1- mnav-pick-max))))
;; (progn (setq mnav-pick-max 5 mnav-pick-cur 10) (mnav-pick-clamp) mnav-pick-cur)

;;; PAINT

(defun mnav-pick-paint-revlink (rev)
  "Render a link to REV into the buffer.
This function should insert a single line of text.
The pointer '=>' and newline are supplied by the caller."
  (if rev
    (insert
     (or (mnav-rev-id     rev) "???") " "
     (or (mnav-rev-date   rev) "???") " "
     (or (mnav-rev-author rev) "???"))
    (insert "-none-")))

(defun mnav-pick-paint-revlink-short (rev)
  "An example of a  function to paint short links."
  (if rev
    (insert
     (or (mnav-rev-id     rev) "???") " "
     (or (mnav-rev-date   rev) "???") " "
     (or (mnav-rev-author rev) "???"))
    (insert "-none-")))


(defun mnav-pick-paint-revlst (label lst min)
  "Paint the buffer with a numbered list links to revisions."
  (insert (format "--- %-10s --------------------\n" label))
  (let ((c 0))
    (dolist (p lst)
      (if (= mnav-pick-cnt mnav-pick-cur)
        (progn
          (setq mnav-pick-point (point))
          (insert "=>"))
        (insert "  "))
      (insert (format "%2d: " mnav-pick-cnt))
      (mnav-pick-paint-revlink p) ;; the data
      (insert "\n")
      (incf mnav-pick-cnt)
      (incf c))
    ;; pad lines to min
    (do ((c c (1+ c))) ((>= c min)) (insert "\n")))
  (insert "\n"))

(defun mnav-pick-paint-selected (rev)
  "Paint the buffer with the selected REV.
This function can be replaced by the user."
  (when (not (mnav-rev-p rev))
    (error "invalid rev to display."))
  (insert "Revision: " (or (mnav-rev-id     rev) "???") "\n"
          "Date:     " (or (mnav-rev-date   rev) "???") "\n"
          "Author:   " (or (mnav-rev-author rev) "???") "\n"
          "Branch:   " (or (mnav-rev-branch rev) "???") "\n"
          "Tag:      " (or (mnav-rev-tag    rev) "???") "\n"
          "\n"
          (or (mnav-rev-changelog rev) "#<none>")))

(defun mnav-pick-paint-buffer (rev)
  "Paint an empty buffer with the selected REV."
  (mnav-pick-paint-revlst "Parents"  (mnav-rev-parents  rev) 3)
  (mnav-pick-paint-revlst "Children" (mnav-rev-children rev) 4)
  (let ((m mnav-rev-mark))
    (if m
      (insert "=== Mark ==========\n  "
              (mnav-rev-id m) "  "
              (mnav-rev-date m) " "
              (mnav-rev-author m) "\n\n")))
  (insert "=== Current Selection ==========\n")
  (mnav-pick-paint-selected rev))
;; (mnav-pick-paint-buffer mnav-rev-point)

 (defun mnav-pick-readkey ()
   "Read until a action is found."
  (let (action key)
    (while (not action)
      (setq key (read-key-sequence (format "Pick: ")))
      (setq action (lookup-key mnav-pick-readkey-map key)))
    action))
;;  (mnav-pick-readkey)

(defun mnav-pick-select (nextrev)
  "Select NEXTREV as the next revision."
  ;; rev = current
  (setf (mnav-rev-pick-cur rev) mnav-pick-cur
        (mnav-rev-pick-back nextrev) rev
        mnav-pick-cur (mnav-rev-pick-cur nextrev)
        rev nextrev))

(defun mnav-pick (&optional revid)
  "Display browser to pick a monotone revision."
  (interactive)
  (when (not mnav-revdb) ; DB loaded?
    (mnav-revdb-reload))
  ;; cast revid to a rec
  (when (not revid) ; default revid
    (setq revid (or mnav-rev-point (monotone-MT-revision))))
  (when (stringp revid) ; cast to a revrec
    (setq revid (mnav-revdb-find revid)))
  (when (not (mnav-rev-p revid))
    (error "revid is not a rev"))
  (setq mnav-rev-point revid)
  ;;
  (let ((buf (get-buffer-create "*monotone rev pick*"))
        (start-buf (current-buffer))
        (rev revid)
        mnav-pick-min mnav-pick-cur mnav-pick-max
        mnav-pick-cnt mnav-pick-point)
    (switch-to-buffer buf)
    ;;
    (catch 'done
      (while t
        ;; clamp
        (setq mnav-pick-cnt 0
              mnav-pick-min 0
              mnav-pick-max (+ (length (mnav-rev-parents rev))
                               (length (mnav-rev-children rev)))
              mnav-rev-point rev)
        (mnav-pick-clamp)
        ;; paint
        (erase-buffer)
        (mnav-pick-paint-buffer rev)
        (goto-char mnav-pick-point)
        ;; prompt & decode
        (setq action (mnav-pick-readkey))
        (cond
         ;; exiting actions
         ((equal action 'quit)
          (setq rev nil)
          (throw 'done nil))
         ((equal action 'pick)
          (throw 'done nil))
         ;; 
         ((commandp action)
          (call-interactively action)
          (setq start-buf nil) ;; done switch back
          (throw 'done nil))
         ;; selecting actions
         ((equal action 'back)
          (let ((back (mnav-rev-pick-back rev)))
            (if back
              (mnav-pick-select back)
              (message "cant go back!"))))
         ((and (numberp action) (<= mnav-pick-min action) (< action mnav-pick-max))
          (mnav-pick-select (mnav-rev-nth-link action rev)))
         ((equal action 'select)
          (mnav-pick-select (mnav-rev-nth-link mnav-pick-cur rev)))
         ;; moving actions
         ((and (equal action 'move-up) (< mnav-pick-min mnav-pick-cur))
          (incf mnav-pick-cur -1))
         ((and (equal action 'move-down) (< mnav-pick-cur mnav-pick-max))
          (incf mnav-pick-cur +1))
         ;; mark
         ((equal action 'swap)
          (if mnav-rev-mark
            (let ((p rev)
                  (m mnav-rev-mark))
            (setq mnav-rev-mark p
                  rev m))))
         ((equal action 'mark)
          (setq mnav-rev-mark rev))
         ((equal action 'unmark)
          (setq mnav-rev-mark nil))
         ;; motion
         ;;((null action) nil)
         (t
          (message "bad action %s" action)
          (sit-for 1)) )
        ;; while & catch
        nil))
    ;;  done
    (when start-buf
      (switch-to-buffer start-buf))
    (kill-buffer buf)
    ;; dont print a huge sexp
    (if (interactive-p) nil rev)))


;;;;;;;;;;

(defun mnav-diff-revisions1 ()
  "Run a diff between the checked out and point revisions."
  (interactive)
  (when (not (and mnav-rev-point))
    (error "You need to choose a revision."))
  (let ((pid (mnav-rev-id mnav-rev-point)))
    (monotone-cmd (list "diff" "--revision" pid))))
;; (mnav-diff-revisions1)

(defun mnav-diff-revisions2 ()
  "Run a diff between the point and mark revisions."
  (interactive)
  (when (not (and mnav-rev-point mnav-rev-mark))
    (error "You need to choose a point and mark with 'mnav-pick'"))
  (let ((pid (mnav-rev-id mnav-rev-point))
        (mid (mnav-rev-id mnav-rev-mark )) )
    (monotone-cmd (list "diff" "--revision" mid "--revision" pid))))
;; (mnav-diff-revisions2)

(defun mnav-diff-file (file)
  "Run a diff between the point and mark revisions."
  (interactive "sEnter monotone file: ")
  (when (not mnav-rev-point)
    (error "You need to choose a point with 'mnav-pick'"))
  (let ((pid (mnav-rev-id mnav-rev-point)))
    (monotone-cmd (list "diff" "--revision" pid file))))
;; (mnav-diff-file "contrib/monotone.el")

;; TESTING:
;; (progn (eval-buffer) (mnav-revdb-reload))
;; (progn (eval-buffer) (message "mnav-pick: %s" (mnav-rev-id (mnav-pick))))
;; (message "mnav-pick: %s" (mnav-rev-id (mnav-pick (monotone-MT-revision))))

(provide 'monotone-nav)

;;; monotone-nav.el ends here
