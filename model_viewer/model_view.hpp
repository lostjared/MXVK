
/**
 * @file model_view.hpp
 * @brief Declares the Qt front end used to launch the MXVK model viewer renderer.
 */

#ifndef MODEL_VIEW_HPP
#define MODEL_VIEW_HPP

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTextCharFormat>
#include <QToolBar>
#include <QVBoxLayout>

/**
 * @brief Main application window for the MXMOD model viewer launcher.
 *
 * MainWindow does not render models directly. It collects model, texture,
 * texture-directory, and resolution options from the user, resolves the
 * command-line viewer executable, launches it as a QProcess, and streams
 * process output into the in-app console.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    /**
     * @brief Constructs the launcher window and restores persisted settings.
     * @param parent Optional Qt parent widget.
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief Persists settings and stops any running renderer process.
     */
    ~MainWindow() override;

  private slots:
    /**
     * @brief Opens a file picker for supported model files.
     */
    void browseModelFile();

    /**
     * @brief Opens a file picker for a texture file or material manifest.
     */
    void browseTextureFile();

    /**
     * @brief Opens a directory picker for external texture resources.
     */
    void browseTextureDirectory();

    /**
     * @brief Validates the selected inputs and launches the viewer process.
     */
    void openModelViewer();

    /**
     * @brief Requests termination of the active viewer process.
     */
    void stopProcess();

    /**
     * @brief Clears the process console and updates the status bar.
     */
    void clearConsole();

    /**
     * @brief Loads a model path selected from the recent-files combo box.
     * @param index Combo-box index of the selected recent model entry.
     */
    void openRecentModel(int index);

    /**
     * @brief Shows application version and feature information.
     */
    void showAboutDialog();

    /**
     * @brief Shows the settings dialog for renderer executable and history options.
     */
    void showSettingsDialog();

    /**
     * @brief Restores persisted model, texture, executable, and window settings.
     */
    void loadSettings();

    /**
     * @brief Persists the current model, texture, executable, and window settings.
     */
    void saveSettings();

    /**
     * @brief Adds a model path to the recent-files list.
     * @param modelPath Absolute or relative model path to add.
     */
    void updateRecentFiles(const QString &modelPath);

    /**
     * @brief Removes the currently selected recent model from history.
     */
    void removeRecentFile();

  private:
    /**
     * @brief Initializes the top-level window and all child UI sections.
     */
    void initWindow();

    /**
     * @brief Builds the application menus and persistent menu actions.
     */
    void setupMenuBar();

    /**
     * @brief Builds the main toolbar and action buttons.
     */
    void setupToolBar();

    /**
     * @brief Builds file selectors, viewer options, and console widgets.
     */
    void setupCentralWidget();

    /**
     * @brief Creates status-bar widgets.
     */
    void setupStatusBar();

    /**
     * @brief Connects long-lived actions and widgets to their slots.
     */
    void createConnections();

    /**
     * @brief Enables or disables controls according to process state.
     * @param processRunning True while the renderer child process is active.
     */
    void updateUIState(bool processRunning);

    /**
     * @brief Revalidates selected paths and updates launch-button availability.
     */
    void validatePaths();

    /**
     * @brief Restores recent model entries from settings into the combo box.
     */
    void loadRecentFiles();

    /**
     * @brief Populates the fixed list of renderer resolution presets.
     */
    void populateResolutionCombo();

    /**
     * @brief Resolves the viewer executable from settings, app-relative paths, or PATH.
     * @return Executable path or command name passed to QProcess.
     */
    QString resolveExecutablePath() const;

  protected:
    /**
     * @brief Confirms shutdown when the viewer process is still running.
     * @param event Close event to accept or ignore.
     */
    void closeEvent(QCloseEvent *event) override;

    /**
     * @brief Accepts drag events that contain file or directory URLs.
     * @param event Drag-enter event supplied by Qt.
     */
    void dragEnterEvent(QDragEnterEvent *event) override;

    /**
     * @brief Assigns dropped model, texture, or directory paths to the matching input.
     * @param event Drop event supplied by Qt.
     */
    void dropEvent(QDropEvent *event) override;

  private:
    ///< Model file path input field.
    QLineEdit *modelLineEdit;

    ///< Texture file or material manifest path input field.
    QLineEdit *textureLineEdit;

    ///< Texture resource directory input field.
    QLineEdit *textureDirLineEdit;

    ///< Read-only console for renderer process output.
    QPlainTextEdit *consoleOutput;

    ///< Currently running renderer process, or nullptr when idle.
    QProcess *activeProcess;

    ///< Starts the renderer process with the selected options.
    QPushButton *openButton;

    ///< Stops the active renderer process.
    QPushButton *stopButton;

    ///< Clears the process console.
    QPushButton *clearButton;

    ///< Resolution preset selector passed to the renderer with -r.
    QComboBox *resolutionCombo;

    ///< Recent model selector populated from persisted settings.
    QComboBox *recentFilesCombo;

    ///< File menu action for choosing a model.
    QAction *openModelAction;

    ///< File menu action for choosing a texture file.
    QAction *openTextureAction;

    ///< File menu action for choosing a texture directory.
    QAction *openTextureDirAction;

    ///< File menu action for clearing console output.
    QAction *clearConsoleAction;

    ///< File menu action for closing the application.
    QAction *exitAction;

    ///< Help menu action for the about dialog.
    QAction *aboutAction;

    ///< Help menu action for viewer executable settings.
    QAction *settingsAction;

    ///< Status-bar text label.
    QLabel *statusLabel;

    ///< Persistent Qt settings store for this launcher.
    QSettings *settings;

    ///< User-configured renderer executable path, or empty for auto-detection.
    QString executablePath;

    ///< Persisted model history, newest first.
    QStringList recentModels;

    ///< Maximum number of recent model paths retained in settings.
    static constexpr int MAX_RECENT_FILES = 10;
};

#endif
