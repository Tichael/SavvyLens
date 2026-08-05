#include "signalviewerwindow.h"
#include "ui_signalviewerwindow.h"
#include "helpwindow.h"
#include "mainwindow.h"
#include "utility.h"
#include <QDebug>
#include <algorithm>

#define NODE_COL    0
#define MSG_COL     1
#define SIGNAL_COL  2
#define VALUE_COL   3

SignalViewerWindow::SignalViewerWindow(const QVector<CANFrame> *frames, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SignalViewerWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    modelFrames = frames;

    ui->tableViewer->setColumnCount(4);
    QStringList headers;
    headers << "Node" << "Message" << "Signal" << "Value";
    ui->tableViewer->setHorizontalHeaderLabels(headers);
    ui->tableViewer->setColumnWidth(0, 100);
    ui->tableViewer->setColumnWidth(1, 150);
    ui->tableViewer->setColumnWidth(2, 150);
    ui->tableViewer->setSortingEnabled(false);

    QSettings settings;
    QFont sysFont;
    int fontSize = settings.value("Main/FontSize", 9).toUInt();
    if(settings.value("Main/FontFixedWidth", false).toBool())
        sysFont = QFontDatabase::systemFont(QFontDatabase::FixedFont); //get default fixed width font
    else
        sysFont = QFont();  //get default font
    sysFont.setPointSize(fontSize);
    ui->tableViewer->setFont(sysFont);

    QHeaderView *HorzHdr = ui->tableViewer->horizontalHeader();
    HorzHdr->setStretchLastSection(true); //causes the data column to automatically fill the tableview
    HorzHdr->setFont(QFont());

    QHeaderView *verticalHeader = ui->tableViewer->verticalHeader();
    verticalHeader->setFont(QFont());

    dbcHandler = DBCHandler::getReference();
    currentlySelectedMsg = nullptr;

    connect(ui->signalTree, SIGNAL(signalChecked(DBC_SIGNAL*)), this, SLOT(addSignal(DBC_SIGNAL*)));
    connect(ui->signalTree, SIGNAL(signalUnchecked(DBC_SIGNAL*)), this, SLOT(removeSignal(DBC_SIGNAL*)));
    connect(MainWindow::getReference(), SIGNAL(framesUpdated(int)), this, SLOT(updatedFrames(int)));
    connect(ui->btnRemove, SIGNAL(clicked(bool)), this, SLOT(removeSelectedSignal()));
    connect(ui->btnSave, SIGNAL(clicked(bool)), this, SLOT(saveSignalsFile()));
    connect(ui->btnLoad, SIGNAL(clicked(bool)), this, SLOT(loadSignalsFile()));
    connect(ui->btnAppend, SIGNAL(clicked(bool)), this, SLOT(appendSignalsFile()));
    connect(ui->btnClear, SIGNAL(clicked(bool)), this, SLOT(clearSignalsTable()));
}

SignalViewerWindow::~SignalViewerWindow()
{
    delete ui;
}

void SignalViewerWindow::updatedFrames(int numFrames)
{
    CANFrame thisFrame;

    if (numFrames == -1) //all frames deleted. Don't care
    {
    }
    else if (numFrames == -2) //all new set of frames. Reset
    {
        for (int i = 0; i < modelFrames->count(); i++)
        {
            thisFrame = modelFrames->at(i);
            processFrame(thisFrame);
        }
    }
    else //just got some new frames. See if they are relevant.
    {
        if (numFrames > modelFrames->count()) return;

        for (int i = modelFrames->count() - numFrames; i < modelFrames->count(); i++)
        {
            thisFrame = modelFrames->at(i);
            processFrame(thisFrame);
        }
    }
}

void SignalViewerWindow::processFrame(CANFrame &frame)
{
    QString sigString;
    DBC_SIGNAL *sig;
    for (int i = 0; i < signalList.count(); i++)
    {
        sig = signalList.at(i);
        if (!sig) return;
        if (sig->parentMessage->ID == frame.frameId())
        {
            if (sig->isSignalInMessage(frame)) //filter out multiplexed signals that aren't in this message.
            {
                if (sig->processAsText(frame, sigString, false)) //if true we could interpret the signal so update it in the list
                {
                    QTableWidgetItem *item = ui->tableViewer->item(i, VALUE_COL);
                    if (!item)
                    {
                        item = new QTableWidgetItem(sigString);
                        ui->tableViewer->setItem(i, VALUE_COL, item);
                    }
                    else item->setText(sigString);
                }
            }
        }
    }
}

void SignalViewerWindow::removeSelectedSignal()
{
    int selRow = ui->tableViewer->currentRow();
    if (selRow < 0) return; //no selected row
    DBC_SIGNAL *sig = signalList.at(selRow);
    ui->signalTree->uncheckSignal(sig);
}

bool compareSignals(DBC_SIGNAL *s1, DBC_SIGNAL *s2) {
    if (s1->parentMessage->sender->name != s2->parentMessage->sender->name)
        return s1->parentMessage->sender->name < s2->parentMessage->sender->name;
    if (s1->parentMessage->name != s2->parentMessage->name)
        return s1->parentMessage->name < s2->parentMessage->name;
    return s1->name < s2->name;
}

void SignalViewerWindow::populateTable()
{
    ui->tableViewer->setRowCount(0);
    std::sort(signalList.begin(), signalList.end(), compareSignals);
    
    for (int i = 0; i < signalList.count(); i++) {
        DBC_SIGNAL *sig = signalList.at(i);
        ui->tableViewer->insertRow(i);
        ui->tableViewer->setItem(i, NODE_COL, new QTableWidgetItem(sig->parentMessage->sender->name));
        ui->tableViewer->setItem(i, MSG_COL, new QTableWidgetItem(sig->parentMessage->name));
        ui->tableViewer->setItem(i, SIGNAL_COL, new QTableWidgetItem(sig->name));
    }
}

void SignalViewerWindow::removeSignal(DBC_SIGNAL *sig)
{
    int idx = signalList.indexOf(sig);
    if (idx >= 0) {
        signalList.removeAt(idx);
        populateTable();
    }
}

void SignalViewerWindow::addSignal(DBC_SIGNAL *sig)
{
    if (!signalList.contains(sig)) {
        signalList.append(sig);
        populateTable();
    }
}

void SignalViewerWindow::saveSignalsFile()
{
    saveDefinitions();
}

void SignalViewerWindow::loadSignalsFile()
{
    loadDefinitions(false);
}

void SignalViewerWindow::appendSignalsFile()
{
    loadDefinitions(true);
}

void SignalViewerWindow::clearSignalsTable()
{
    clearSignalsTable(true);
}

void SignalViewerWindow::clearSignalsTable(bool askForConfirmation)
{
    if(askForConfirmation)
    {
        QMessageBox::StandardButton confirmDialog;
        confirmDialog = QMessageBox::question(this, "Danger Will Robinson", "Are you sure you want to clear all of your signals?",
                                      QMessageBox::Yes|QMessageBox::No);
        if (confirmDialog == QMessageBox::No)
        {
            return;
        }
    }

    ui->signalTree->uncheckAll();
    signalList.clear();
    ui->tableViewer->setRowCount(0);
}

void SignalViewerWindow::saveDefinitions()
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("SignalViewer definition (*.sdf)")));

    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(settings.value("SignalViewer/LoadSaveDirectory", dialog.directory().path()).toString());

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        settings.setValue("SignalViewer/LoadSaveDirectory", dialog.directory().path());

        if (!filename.contains('.')) filename += ".sdf";

        QFile *outFile = new QFile(filename);

        if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
            return;

        DBC_SIGNAL *sig;
        for (int i = 0; i < signalList.count(); i++)
        {
            sig = signalList.at(i);

            outFile->write("SV1");
            outFile->putChar(',');
            outFile->write(QString::number(sig->parentMessage->ID, 16).toUtf8());
            outFile->putChar(',');
            outFile->write(sig->parentMessage->name.toUtf8());
            outFile->putChar(',');
            outFile->write(sig->name.toUtf8());

            outFile->write("\n");
        }
        outFile->close();
    }
}

void SignalViewerWindow::loadDefinitions(bool append)
{
    QString filename;
    QFileDialog dialog;
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("SignalViewer definition (*.sdf)")));

    QList<DBC_SIGNAL *> loadedSignals;

    if (dbcHandler == nullptr) return;
    if (dbcHandler->getFileCount() == 0) dbcHandler->createBlankFile();

    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setDirectory(settings.value("SignalViewer/LoadSaveDirectory", dialog.directory().path()).toString());

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        settings.setValue("SignalViewer/LoadSaveDirectory", dialog.directory().path());

        QFile *inFile = new QFile(filename);
        QByteArray line;

        if (!inFile->open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        while (!inFile->atEnd()) {
            line = inFile->readLine().simplified();
            if (line.length() > 2)
            {
                QList<QByteArray> tokens = line.split(',');

                DBC_SIGNAL *sig;

                if (tokens[0] == "SV1") //signal viewer save format v1
                {
                    // = tokens[1].toUInt(nullptr, 16);

                    int msgId = tokens[1].toUInt(nullptr, 16);
                    QString msgName = QString(tokens[2]);
                    QString sigName = QString(tokens[3]);
                    DBC_MESSAGE *msg;;
                    if( (msg = dbcHandler->findMessage(msgId)) )
                    {
                        sig = msg->sigHandler->findSignalByName(sigName);
                        if(sig)
                            loadedSignals.append(sig);
                    }
                    else if ( (msg = dbcHandler->findMessage(msgName)) )
                    {
                        //this is not a very safe way to match since messages names can be duplicated
                        sig = msg->sigHandler->findSignalByName(sigName);
                        if(sig)
                            loadedSignals.append(sig);
                    }
                    else
                    {
                        qDebug() << "Couldn't find the message by ID or name! " << msgName << "  " << sigName;
                    }
                }
            }
        }
        inFile->close();

        if(loadedSignals.count() > 0)
        {
            if(append == false)
            {
                clearSignalsTable(false);
            }

            for (int i=0; i<loadedSignals.count(); i++)
            {
                ui->signalTree->checkSignal(loadedSignals[i]);
            }
        }
    }
}

void SignalViewerWindow::openForSignal(int messageId, QString signalName)
{
    if (!isVisible()) show();
    
    if (dbcHandler == nullptr) return;
    
    DBC_MESSAGE *msg = dbcHandler->findMessage(messageId);
    if (!msg) return;
    
    DBC_SIGNAL *sig = msg->sigHandler->findSignalByName(signalName);
    if (!sig) return;
    
    // Check if it's already in the list to avoid duplicates
    for (int i = 0; i < signalList.count(); i++) {
        if (signalList.at(i) == sig) {
            return;
        }
    }
    
    addSignal(sig);
}
