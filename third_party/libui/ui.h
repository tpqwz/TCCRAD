#ifndef UI_H
#define UI_H

/*
 * The calls tccrad emits for the libui backend.
 * The names follow libui-ng (the maintained fork of andlabs/libui).
 * This file is the X11 stand-in for those calls. It does not link GTK.
 */

#include <stddef.h>

typedef struct uiControl uiControl;
typedef struct uiWindow uiWindow;
typedef struct uiButton uiButton;
typedef struct uiBox uiBox;
typedef struct uiLabel uiLabel;
typedef struct uiCheckbox uiCheckbox;
typedef struct uiEntry uiEntry;
typedef struct uiMultilineEntry uiMultilineEntry;
typedef struct uiSlider uiSlider;
typedef struct uiCombobox uiCombobox;
typedef struct uiRadioButtons uiRadioButtons;
typedef struct uiProgressBar uiProgressBar;
typedef struct uiSpinbox uiSpinbox;
typedef struct uiGroup uiGroup;
typedef struct uiTab uiTab;
typedef struct uiMenu uiMenu;
typedef struct uiMenuItem uiMenuItem;

typedef struct uiInitOptions {
    size_t Size;
} uiInitOptions;

#define uiControl(x) ((uiControl *)(void *)(x))

const char *uiInit(uiInitOptions *o);
void uiFreeInitError(const char *e);
void uiUninit(void);
void uiMain(void);
void uiQuit(void);

uiWindow *uiNewWindow(const char *title, int width, int height, int has_menu);
void uiWindowSetMargined(uiWindow *w, int margined);
void uiWindowOnClosing(uiWindow *w, int (*f)(uiWindow *, void *), void *data);
void uiWindowOnContentSizeChanged(uiWindow *w, void (*f)(uiWindow *, void *), void *data);
void uiWindowOnFocusChanged(uiWindow *w, void (*f)(uiWindow *, void *), void *data);
int uiWindowFocused(uiWindow *w);
void uiWindowSetResizeable(uiWindow *w, int resizeable);
void uiWindowSetChild(uiWindow *w, uiControl *child);
void uiWindowSetBorderless(uiWindow *w, int borderless);
void uiWindowSetFullscreen(uiWindow *w, int fullscreen);
void uiControlShow(uiControl *c);

uiBox *uiNewVerticalBox(void);
uiBox *uiNewHorizontalBox(void);
void uiBoxSetPadded(uiBox *b, int padded);
void uiBoxAppend(uiBox *b, uiControl *c, int stretch);

uiLabel *uiNewLabel(const char *text);
uiButton *uiNewButton(const char *text);
void uiButtonOnClicked(uiButton *b, void (*f)(uiButton *, void *), void *data);

uiCheckbox *uiNewCheckbox(const char *text);
void uiCheckboxSetChecked(uiCheckbox *c, int checked);
int uiCheckboxChecked(uiCheckbox *c);
void uiCheckboxOnToggled(uiCheckbox *c, void (*f)(uiCheckbox *, void *), void *data);

uiEntry *uiNewEntry(void);
uiEntry *uiNewPasswordEntry(void);
void uiEntrySetText(uiEntry *e, const char *text);
char *uiEntryText(uiEntry *e);
void uiEntryOnChanged(uiEntry *e, void (*f)(uiEntry *, void *), void *data);

uiMultilineEntry *uiNewMultilineEntry(void);
void uiMultilineEntrySetText(uiMultilineEntry *e, const char *text);
char *uiMultilineEntryText(uiMultilineEntry *e);
void uiMultilineEntryOnChanged(uiMultilineEntry *e,
    void (*f)(uiMultilineEntry *, void *), void *data);
void uiFreeText(char *text);

uiSlider *uiNewSlider(int min, int max);
void uiSliderSetValue(uiSlider *s, int value);
int uiSliderValue(uiSlider *s);
void uiSliderOnChanged(uiSlider *s, void (*f)(uiSlider *, void *), void *data);
void uiSliderOnReleased(uiSlider *s, void (*f)(uiSlider *, void *), void *data);

uiCombobox *uiNewCombobox(void);
void uiComboboxAppend(uiCombobox *c, const char *text);
void uiComboboxSetSelected(uiCombobox *c, int n);
int uiComboboxSelected(uiCombobox *c);
void uiComboboxOnSelected(uiCombobox *c, void (*f)(uiCombobox *, void *), void *data);

uiRadioButtons *uiNewRadioButtons(void);
void uiRadioButtonsAppend(uiRadioButtons *r, const char *text);
void uiRadioButtonsSetSelected(uiRadioButtons *r, int n);
int uiRadioButtonsSelected(uiRadioButtons *r);
void uiRadioButtonsOnSelected(uiRadioButtons *r, void (*f)(uiRadioButtons *, void *), void *data);

uiProgressBar *uiNewProgressBar(void);
void uiProgressBarSetValue(uiProgressBar *p, int n);

uiSpinbox *uiNewSpinbox(int min, int max);
void uiSpinboxSetValue(uiSpinbox *s, int value);
int uiSpinboxValue(uiSpinbox *s);
void uiSpinboxOnChanged(uiSpinbox *s, void (*f)(uiSpinbox *, void *), void *data);

void *uiNewHorizontalSeparator(void);

uiGroup *uiNewGroup(const char *title);
void uiGroupSetMargined(uiGroup *g, int margined);
void uiGroupSetChild(uiGroup *g, uiControl *c);

uiTab *uiNewTab(void);
void uiTabAppend(uiTab *t, const char *name, uiControl *page);

uiMenu *uiNewMenu(const char *name);
uiMenuItem *uiMenuAppendItem(uiMenu *m, const char *name);
void uiMenuItemOnClicked(uiMenuItem *item,
    void (*f)(uiMenuItem *, uiWindow *, void *), void *data);

typedef struct uiTableValue uiTableValue;
typedef struct uiTableModel uiTableModel;
typedef struct uiTableModelHandler uiTableModelHandler;
typedef struct uiTable uiTable;
typedef unsigned int uiTableValueType;
enum {
    uiTableValueTypeString = 0,
    uiTableValueTypeImage = 1,
    uiTableValueTypeInt = 2,
    uiTableValueTypeColor = 3
};
struct uiTableModelHandler {
    int (*NumColumns)(uiTableModelHandler *, uiTableModel *);
    uiTableValueType (*ColumnType)(uiTableModelHandler *, uiTableModel *, int);
    int (*NumRows)(uiTableModelHandler *, uiTableModel *);
    uiTableValue *(*CellValue)(uiTableModelHandler *, uiTableModel *, int, int);
    void (*SetCellValue)(uiTableModelHandler *, uiTableModel *, int, int, const uiTableValue *);
};
typedef struct uiTableTextColumnOptionalParams uiTableTextColumnOptionalParams;
struct uiTableTextColumnOptionalParams {
    int ColorModelColumn;
};
typedef struct uiTableParams uiTableParams;
struct uiTableParams {
    uiTableModel *Model;
    int RowBackgroundColorModelColumn;
};
#define uiTableModelColumnNeverEditable (-1)
uiTableValue *uiNewTableValueString(const char *str);
void uiFreeTableValue(uiTableValue *v);
uiTableModel *uiNewTableModel(uiTableModelHandler *mh);
uiTable *uiNewTable(uiTableParams *params);
void uiTableAppendTextColumn(uiTable *t, const char *name, int textModelColumn,
    int textEditableModelColumn, uiTableTextColumnOptionalParams *textParams);
void uiTableHeaderSetVisible(uiTable *t, int visible);
void uiTableOnRowClicked(uiTable *t, void (*f)(uiTable *, int, void *), void *data);
void uiTableOnRowDoubleClicked(uiTable *t, void (*f)(uiTable *, int, void *), void *data);

#endif
