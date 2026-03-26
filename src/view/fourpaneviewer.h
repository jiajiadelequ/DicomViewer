#pragma once

#include "src/model/studyloadresult.h"

#include <QWidget>
#include <vtkSmartPointer.h>

class QLabel;
class QListWidget;
class ModelViewWidget;
class MprViewWidget;
class QStackedLayout;
class QWidget;
class vtkImageData;

class FourPaneViewer final : public QWidget
{
    Q_OBJECT

public:
    explicit FourPaneViewer(QWidget *parent = nullptr);

    bool applyStudyLoadResult(const StudyLoadResult &result, QString *errorMessage = nullptr);
    void showEmptyState();
    void showLoadingState(const QString &message);
    void showErrorState(const QString &message);

private:
    void ensureContentPage();
    void updateSummary(const StudyPackage &package);

    QStackedLayout *m_rootLayout;
    QWidget *m_statePage;
    QLabel *m_stateTitleLabel;
    QLabel *m_stateMessageLabel;
    QWidget *m_contentPage;

    MprViewWidget *m_axialPanel;
    MprViewWidget *m_coronalPanel;
    MprViewWidget *m_sagittalPanel;
    ModelViewWidget *m_volumePanel;
    QListWidget *m_objectList;
    QLabel *m_summaryLabel;
    vtkSmartPointer<vtkImageData> m_imageData;
};
