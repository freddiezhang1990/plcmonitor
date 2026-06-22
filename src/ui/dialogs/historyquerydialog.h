#ifndef HISTORYQUERYDIALOG_H
#define HISTORYQUERYDIALOG_H

#include <QDialog>

class PLCData;
class QListWidget;

class HistoryQueryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryQueryDialog(PLCData* plcData,
                                const QStringList& preselectedTags = QStringList(),
                                QWidget *parent = nullptr);
    ~HistoryQueryDialog();

    QStringList getSelectedTags() const;

private:
    void setupUI();
    void populateTagList(const QStringList& preselectedTags);

    PLCData* m_plcData;
    QListWidget* m_tagList;
};

#endif // HISTORYQUERYDIALOG_H