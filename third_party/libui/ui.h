#ifndef UI_H
#define UI_H

/*
 * The calls tccrad emits for the libui backend.
 * This is an X11 implementation of that surface. The andlabs library
 * needs GTK headers, which are not required here.
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

#endif
