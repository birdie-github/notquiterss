# Folder properties and bulk feed configuration

Folder Properties configures the folder's own combined article view. General
contains its name and a Disable checkbox; Display controls image loading and text
direction; Columns controls its columns and sorting. Status remains informational.
Saving Properties leaves descendants unchanged unless the Disable checkbox differs
from its initial state. It starts checked only when all descendant feeds are disabled;
mixed folders start unchecked, and empty folders have the control disabled. A count
and explanation make its scope explicit. Checking it disables all descendant feeds;
unchecking it enables them all, replacing their individual disabled states. Schedules
and timer counters are preserved. Folder edits and state changes are atomic. This is
a one-time operation, not a persistent restriction on newly added or moved feeds.

## Tools → Bulk configure feeds

The command appears directly below Article Filters. There is no bulk command on
a folder's context menu, and the main feed tree's selection is not used.

Choose an action on the left and check targets in the tree on the right. Nothing
is checked when the dialog opens. Checking a folder checks all descendant feeds;
unchecking some of them makes the folder partially checked. Empty folders select
nothing. Highlighting a row is not selection for Apply: checkboxes determine the
targets. A count shows the number of distinct checked feeds.

Actions are:

- Enable/disable: replace only each selected feed's individual state.
- Update schedule: global settings, a custom interval, or no scheduled updates.
- Image loading: never, global settings, or always.
- Text direction: enable or disable right-to-left layout.
- Columns and sorting: replace the column layout and sorting together.

Apply affects only the displayed action and checked feeds, never folder records.
Switching actions does not apply the previous action. Targets stay checked after
Apply so another action can be applied to the same feeds. The target tree is a
snapshot when opened; feeds added later are not silently included. Reopen the
dialog to see newly added feeds.

Apply remains disabled until an action and at least one feed are selected. The
first Apply in each application session requires confirmation naming the action
and count. Cancel does not suppress the next confirmation. Confirming Apply
suppresses further confirmations until restart; nothing is written to the INI.
Apply leaves the dialog open and reports success. Close does not undo previously
applied changes. Failed database writes roll back the entire operation and leave
choices available for retry. A transaction does not force an in-memory database
to disk; existing save/exit behavior remains in force.

## Disabled state and defaults

A disabled feed cannot start automatic or manual updates; already-submitted
requests are not cancelled. Enabling or disabling a feed leaves its schedule
untouched. No scheduled updates still permits startup and manual updates.
"Global" means application settings, never a parent folder.

A folder is grey only when it contains at least one feed and every descendant
feed is disabled. Its tooltip reports disabled/total counts. Empty or mixed
folders remain normal. This is derived presentation, not a stored restriction:
no child states or schedules are changed by the colour itself. Existing legacy
folder disabled flags are ignored; the Properties checkbox derives its initial
value from the actual descendant feeds. Adding/enabling/moving a feed can change the appearance of its
ancestor folders. A selected row still uses the normal selection colours.

New feeds use database/application defaults rather than copying parent-folder
settings. Existing feed settings remain stored as before.

Image loading and article text direction follow the currently viewed feed or
folder. The same article can be displayed differently through its feed and
through a folder. Bulk feed edits do not replace folder-view settings. Individual
article-list rows retain their source feed's alignment.

## Verification

`tests/feedbulksettings/feedbulksettings.pro` covers explicit target scope,
independent values (including false/zero), schedule modes and transaction rollback.
`tests/feedselectiontree/feedselectiontree.pro` covers unchecked initial state,
nested and partial selection, collapsed/empty folders and Filter Rules preselection.
Both require Qt SQL and the SQLite driver; the tree tests also require Qt Widgets.

Manual checks on Qt5/Qt6 builds:

- Open from Tools with an unrelated main-tree selection. All checkboxes must be
  clear. Check folders, uncheck individual children, and select feeds elsewhere.
  Confirm partial states, counts and absence of duplicate targets.
- Apply one action, switch to another and apply again. Verify only those checked
  feeds and the current action change; targets remain checked afterward.
- Cancel the first confirmation, retry and confirm. Further Apply operations in
  this session should not prompt; restarting must restore confirmation.
- Disable every feed inside a nested folder and check ancestor greying/tooltips.
  Re-enable one feed; its existing schedule must survive and folders become mixed.
- Rename a folder and change its view settings. Verify child feed settings remain
  unchanged, including columns, and inspect already-open normal/newspaper views.
- Open Article Filters and edit an existing filter, then create a filter through
  a feed's context menu. Verify its tree ordering, preselection and saved scope.
- Add a feed to a grey/customized folder: it should use defaults, not a copied
  disabled state or schedule. Repeat persistence checks with Store DB in memory
  enabled, using a normal exit/restart and without adding a save-on-Apply path.

Folder Disable follow-up checks:

- For a mixed nested folder, save a rename without touching Disable. Verify each
  feed retains its state. Cancel after changing Disable must also leave all states.
- Check Disable and save: all descendant feeds become disabled, including those in
  subfolders; outside feeds and all schedules remain unchanged.
- Reopen, uncheck Disable and save: every descendant feed becomes enabled, including
  feeds that were individually disabled before. Folder greying updates accordingly.
- Empty folders show an unchecked, disabled control. Repeat with an in-memory
  database and confirm the saved states after normal exit/restart.
