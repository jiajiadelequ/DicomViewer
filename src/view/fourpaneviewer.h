#pragma once

#include "src/model/studypackage.h"

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

    bool loadStudyPackage(const StudyPackage &package, QString *errorMessage = nullptr);
    void showEmptyState();
    void showLoadingState(const QString &message);
    void showErrorState(const QString &message);

private:
    bool loadDicomSeries(const QString &dicomPath, QString *errorMessage = nullptr);
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
