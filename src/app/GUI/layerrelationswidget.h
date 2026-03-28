#ifndef LAYERRELATIONSWIDGET_H
#define LAYERRELATIONSWIDGET_H

#include <QMetaObject>
#include <QPointer>
#include <QWidget>

class Document;
class Canvas;
class BoundingBox;
class QComboBox;
class QLabel;

class LayerRelationsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LayerRelationsWidget(Document &document,
                                  QWidget *parent = nullptr);

    void setCurrentScene(Canvas *scene);
    void setCurrentBox(BoundingBox *box);

private:
    void rebuildParentChoices();
    void syncControls();
    void setControlsEnabled(bool enabled);

    Document &mDocument;
    QPointer<Canvas> mCurrentScene;
    QPointer<BoundingBox> mCurrentBox;

    QComboBox *mParentCombo = nullptr;
    QComboBox *mMatteCombo = nullptr;
    QLabel *mStatusLabel = nullptr;
    QLabel *mHintLabel = nullptr;

    QMetaObject::Connection mSceneSelectionConn;
    QMetaObject::Connection mBoxBlendConn;
    bool mUpdatingControls = false;
};

#endif // LAYERRELATIONSWIDGET_H
