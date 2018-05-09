/*
###############################################################################
#
#  EGSnrc egs++ geometry viewer ciew control extensions
#  Copyright (C) 2015 National Research Council Canada
#
#  This file is part of EGSnrc.
#
#  EGSnrc is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Affero General Public License as published by the
#  Free Software Foundation, either version 3 of the License, or (at your
#  option) any later version.
#
#  EGSnrc is distributed in the hope that it will be useful, but WITHOUT ANY
#  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
#  FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
#  more details.
#
#  You should have received a copy of the GNU Affero General Public License
#  along with EGSnrc. If not, see <http://www.gnu.org/licenses/>.
#
###############################################################################
#
#  Author:          Iwan Kawrakow, 2005
#
#  Contributors:    Frederic Tessier
#                   Ernesto Mainegra-Hing
#                   Manuel Stoeckl
#                   Reid Townson
#
###############################################################################
*/

#include "image_window.h"
#include "saveimage.h"
#include "clippingplanes.h"
#include "viewcontrol.h"

#include "egs_libconfig.h"
#include "egs_functions.h"
#include "egs_base_geometry.h"
#include "egs_visualizer.h"
#include "egs_timer.h"
#include "egs_input.h"

#include <qmessagebox.h>
#include <qapplication.h>
#include <qstring.h>
#include <qcolordialog.h>
#include <qimage.h>
#include <qpixmap.h>
#include <qpainter.h>
#include <qfile.h>
#include <qfiledialog.h>
#include <qprogressdialog.h>
#include <QTextStream>

#include <cmath>
#include <cstdlib>
#include <vector>
using namespace std;

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

static unsigned char standard_red[] = {
    255,   0,   0,   0, 255, 255, 128,   0,   0,   0, 128, 128, 191, 80,
    0,  0,   0,  0,  85, 255, 192, 128
};
static unsigned char standard_green[] = {
    0, 255,   0, 255,   0, 255,   0, 128,   0, 128,   0, 128,   0,  0,
    191, 80,   0,  0, 170, 170, 192, 128
};
static unsigned char standard_blue[] = {
    0,   0, 255, 255, 255,   0,   0,   0, 128, 128, 128,   0,   0,  0,
    0,  0, 191, 80, 127, 127, 192, 128
};

GeometryViewControl::GeometryViewControl(QWidget *parent, const char *name)
    : QMainWindow(parent) {
    setObjectName(name);
    setupUi(this);

#ifdef VIEW_DEBUG
    egsWarning("In init()\n");
#endif
    g = 0;
    theta = 0;
    c_theta = cos(theta);
    s_theta = sin(theta);
    phi = 0;
    c_phi = cos(phi);
    s_phi = sin(phi);
    a_light = 0.25;
    int ilight = (int)(a_light*ambientLight->maximum());
    ambientLight->blockSignals(true);
    ambientLight->setValue(ilight);
    ambientLight->blockSignals(false);
    distance = 25;

    zoomlevel = -112;
    size = 1;
    look_at = EGS_Vector();
    setLookAtLineEdit();
    setProjectionLineEdit();
    p_light = EGS_Vector(0,0,distance);
    setLightLineEdit();
    camera = p_light;
    screen_xo = EGS_Vector();
    screen_v1 = EGS_Vector(1,0,0);
    screen_v2 = EGS_Vector(0,1,0);

    // various state variables
    showAxes = this->showAxesCheckbox->isChecked();
    showAxesLabels = this->showAxesLabelsCheckbox->isChecked();
    showTracks = this->showTracksCheckbox->isChecked();
    showPhotonTracks = this->showPhotonsCheckbox->isChecked();
    showElectronTracks = this->showElectronsCheckbox->isChecked();
    showPositronTracks = this->showPositronsCheckbox->isChecked();

    // camera orientation vectors (same as the screen vectors)
    camera_v1 = screen_v1;
    camera_v2 = screen_v2;

    // camera home position
    camera_home = camera;
    camera_home_v1 = camera_v1;
    camera_home_v2 = camera_v2;
    zoomlevel_home = zoomlevel;

    m_colors = 0;
    nmed = 0;

    // Set default colors
    backgroundColor = QColor(0,0,0);
    textColor = QColor(255,255,255);
    axisColor = QColor(255,255,255);
    photonColor = QColor(255,255,0);
    electronColor = QColor(255,0,0);
    positronColor = QColor(0,0,255);
    energyScaling = this->energyScalingCheckbox->isChecked();
    initColorSwatches();

    gview = new ImageWindow(this,"gview");
    gview->resize(512,512);
    gview->showRegions(this->showRegionsCheckbox->isChecked());

    // connect signals and slots for mouse navigation
    connect(gview, SIGNAL(cameraRotation(int, int)), this, SLOT(cameraRotate(int, int)));
    connect(gview, SIGNAL(cameraZooming(int)), this, SLOT(cameraZoom(int)));
    connect(gview, SIGNAL(cameraHoming()), this, SLOT(cameraHome()));
    connect(gview, SIGNAL(cameraHomeDefining()), this, SLOT(cameraHomeDefine()));
    connect(gview, SIGNAL(cameraTranslating(int, int)), this, SLOT(cameraTranslate(int, int)));
    connect(gview, SIGNAL(cameraRolling(int)), this, SLOT(cameraRoll(int)));
    connect(gview, SIGNAL(putCameraOnAxis(char)), this, SLOT(cameraOnAxis(char)));
    connect(gview, SIGNAL(leftMouseClick(int,int)), this, SLOT(reportViewSettings(int,int)));
    connect(gview, SIGNAL(leftDoubleClick(EGS_Vector)), this, SLOT(setRotationPoint(EGS_Vector)));

    save_image = new SaveImage(this,"save image");

    cplanes = new ClippingPlanesWidget;
    connect(cplanes,SIGNAL(clippingPlanesChanged()),
            this,SLOT(setClippingPlanes()));

    // Add the clipping planes widget to the designated layout
    clipLayout->addWidget(cplanes);
    clipLayout->addSpacing(86);

    // set the widget to show near the left-upper corner of the screen
    move(QPoint(25,25));
}

GeometryViewControl::~GeometryViewControl() {
}

void GeometryViewControl::selectInput() {
    QFileInfo inputFileInfo = QFileInfo(filename);
    QString input_file = QFileDialog::getOpenFileName(this, "Select geometry definition file", inputFileInfo.canonicalPath());

    if (input_file.isEmpty()) {
        return;
    }

    this->show();
    this->setFilename(input_file);

    if (!this->loadInput(false)) {
        egsWarning("GeometryViewControl::selectInput: Error: The input file could not be loaded: %s\n", input_file.toLatin1().data());
    }

    // Reset the transparency bar since it won't apply for the new media
    changeTransparency(255);
    transparency->setValue(255);
}

bool GeometryViewControl::loadInput(bool reloading) {
    #ifdef VIEW_DEBUG
        egsWarning("In loadInput(), reloading is %d\n",reloading);
    #endif

    // check that the file (still) exists
    QFile file(filename);
    if (!file.exists()) {
        egsWarning("\nFile %s does not exist anymore!\n\n",filename.toUtf8().constData());
        return false;
    }

    QFileInfo fileInfo = QFileInfo(file);

    // Set the title of the viewer
    this->setProperty("windowTitle", "View Controls ("+fileInfo.baseName()+")");
    gview->setProperty("windowTitle", "egs_view ("+fileInfo.baseName()+")");

    // Read the input file
    EGS_Input input;
    input.setContentFromFile(filename.toUtf8().constData());

    // clear the current geometry
    gview->stopWorker();
    qApp->processEvents();

    // delete geometry
    if (g) {
        delete g;
        g = 0;
    }

    // Have to clear geometries twice to get all the inactive ones
    EGS_BaseGeometry::clearGeometries();
    EGS_BaseGeometry::clearGeometries();

    // Load the new geometry
    EGS_BaseGeometry *newGeom = EGS_BaseGeometry::createGeometry(&input);
    if (!newGeom) {
        QMessageBox::critical(this,"Geometry error",
                "The geometry is not correctly defined. Edit the input file and reload.",QMessageBox::Ok,0,0);

        return false;
    }

    // restart from scratch (copied from main.cpp)
    EGS_Float xmin = -50, xmax = 50;
    EGS_Float ymin = -50, ymax = 50;
    EGS_Float zmin = -50, zmax = 50;
    EGS_Input *vc = input.takeInputItem("view control");
    std::vector<EGS_UserColor> user_colors;
    if (vc) {
        EGS_Float tmp;
        if (!vc->getInput("xmin",tmp)) {
            xmin = tmp;
        }
        if (!vc->getInput("xmax",tmp)) {
            xmax = tmp;
        }
        if (!vc->getInput("ymin",tmp)) {
            ymin = tmp;
        }
        if (!vc->getInput("ymax",tmp)) {
            ymax = tmp;
        }
        if (!vc->getInput("zmin",tmp)) {
            zmin = tmp;
        }
        if (!vc->getInput("zmax",tmp)) {
            zmax = tmp;
        }
        EGS_Input *uc;
        while ((uc = vc->takeInputItem("set color")) != 0) {
            vector<string> inp;
            int err = uc->getInput("set color",inp);
            if (!err && (inp.size() == 4 || inp.size() == 5)) {
                EGS_UserColor ucolor;
                ucolor.medname = inp[0];
                sscanf(inp[1].c_str(),"%d",&ucolor.red);
                sscanf(inp[2].c_str(),"%d",&ucolor.green);
                sscanf(inp[3].c_str(),"%d",&ucolor.blue);
                if (inp.size() == 5) {
                    sscanf(inp[4].c_str(),"%d",&ucolor.alpha);
                }
                else {
                    ucolor.alpha = 255;
                }
                user_colors.push_back(ucolor);
            }
            else {
                qWarning("Wrong 'set color' input");
            }
            delete uc;
        }
        delete vc;
    }

    // Only allow region selection for up to 1k regions
    int nreg = newGeom->regions();
    if(nreg < 1001) {
        allowRegionSelection = true;
        show_regions.resize(nreg,true);
    } else {
        allowRegionSelection = false;
        egsInformation("Region selection tab has been disabled due to >1000 regions (for performance reasons)\n");
    }
    tabWidget->setTabEnabled(2,allowRegionSelection);

    // Get the rendering parameters
    RenderParameters &rp = gview->pars;
    rp.allowRegionSelection = allowRegionSelection;

    gview->restartWorker();
    setGeometry(newGeom,user_colors,xmin,xmax,ymin,ymax,zmin,zmax,reloading);

    if(allowRegionSelection) {
        updateRegionTable();
    }

    return true;
}

void GeometryViewControl::reloadInput() {
    if(!loadInput(true)) {
        egsWarning("GeometryViewControl::reloadInput: Error: The geometry is not correctly defined\n");
    }
}

void GeometryViewControl::saveConfig() {
#ifdef VIEW_DEBUG
    egsWarning("In saveConfig()\n");
#endif

    // Set a default config file name
    // These configs will have the .egsview extension
    QFileInfo inputFileInfo = QFileInfo(filename);
    QString defaultFilename = inputFileInfo.canonicalPath() + "/" + inputFileInfo.completeBaseName() + ".egsview";

    // Prompt the user for a filename and open the file for writing
    QString configFilename = QFileDialog::getSaveFileName(this, "Save config file as...", defaultFilename, "*.egsview");
    QFile configFile(configFilename);
    if(!configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    QTextStream out(&configFile);

    // Get the rendering parameters
    RenderParameters &rp = gview->pars;

    out << ":start image size:" << endl;
    out << "    nx = " << rp.nx << endl;
    out << "    ny = " << rp.ny << endl;
    out << ":stop image size:" << endl;

    out << ":start camera view:" << endl;
    out << "    camera position = " << lookX->text() << " "
            << lookY->text() << " "
            << lookZ->text() << endl;
    out << "    camera = " << camera.x << " "
            << camera.y << " "
            << camera.z << endl;
    out << "    camera v1 = " << camera_v1.x << " "
            << camera_v1.y << " "
            << camera_v1.z << endl;
    out << "    camera v2 = " << camera_v2.x << " "
            << camera_v2.y << " "
            << camera_v2.z << endl;
    out << "    zoom = " << zoomlevel << endl;
    out << ":stop camera view:" << endl;

    out << ":start home view:" << endl;
    out << "    home position = " << look_at_home.x << " "
            << look_at_home.y << " "
            << look_at_home.z << endl;
    out << "    home = " << camera_home.x << " "
            << camera_home.y << " "
            << camera_home.z << endl;
    out << "    home v1 = " << camera_home_v1.x << " "
            << camera_home_v1.y << " "
            << camera_home_v1.z << endl;
    out << "    home v2 = " << camera_home_v2.x << " "
            << camera_home_v2.y << " "
            << camera_home_v2.z << endl;
    out << "    zoom = " << zoomlevel_home << endl;
    out << ":stop home view:" << endl;

    out << ":start tracks:" << endl;
    out << "    show tracks = " << showTracks << endl;
    out << "    photons = " << showPhotonTracks << endl;
    out << "    electrons = " << showElectronTracks << endl;
    out << "    positrons = " << showPositronTracks << endl;
    out << ":stop tracks:" << endl;

    out << ":start overlay:" << endl;
    out << "    show axis = " << showAxes << endl;
    out << "    show axis labels = " << showAxesLabels << endl;
    out << "    show regions = " << showRegionsCheckbox->isChecked() << endl;
    out << ":stop overlay:" << endl;

    if(rp.material_colors.size() > 0) {
        out << ":start material colors:" << endl;
        for (size_t i=0; i<rp.material_colors.size(); ++i) {
            out << "    :start material:" << endl;
            if(i==size_t(nmed)) {
                out << "        material = vacuum" << endl;
            } else {
                out << "        material = " << g->getMediumName(i) << endl;
            }
            out << "        rgb = " << qRed(m_colors[i]) << " "
                            << qGreen(m_colors[i]) << " "
                            << qBlue(m_colors[i]) << endl;
            out << "        alpha = " << qAlpha(m_colors[i]) << endl;
            out << "    :stop material:" << endl;
        }
        out << ":stop material colors:" << endl;
    }

    out << ":start clipping planes:" << endl;
    for (int i=0; i<cplanes->numPlanes(); i++) {
        QTableWidgetItem *itemAx = cplanes->getItem(i,0),
                     *itemAy = cplanes->getItem(i,1),
                     *itemAz = cplanes->getItem(i,2),
                     *itemD = cplanes->getItem(i,3),
                     *itemApplied = cplanes->getItem(i,4);

        out << "    :start plane:" << endl;
        if(itemAx) {
            out << "        ax = " << itemAx->text() << endl;
        }
        if(itemAy) {
            out << "        ay = " << itemAy->text() << endl;
        }
        if(itemAz) {
            out << "        az = " << itemAz->text() << endl;
        }
        if(itemD) {
            out << "        d = " << itemD->text() << endl;
        }
        if(itemApplied) {
            out << "        applied = " << itemApplied->checkState() << endl;
        }
        out << "    :stop plane:" << endl;
    }
    out << ":stop clipping planes:" << endl;

    out << ":start hidden regions:" << endl;
    out << "    region list =";
    for(size_t i = 0; i < show_regions.size(); ++i) {
        // List all the unchecked regions
        if(!show_regions[i]) {
            QTableWidgetItem *itemRegion = regionTable->item(i,0);
            if(itemRegion) {
                out << " " << itemRegion->text();
            }
        }
    }
    out << endl;
    out << ":stop hidden regions:" << endl;

    out << ":start colors:" << endl;
    out << "    background = " << backgroundColor.red() << " " <<
            backgroundColor.green() << " " <<
            backgroundColor.blue() << endl;
    out << "    text = " << textColor.red() << " " <<
            textColor.green() << " " <<
            textColor.blue() << endl;
    out << "    axis = " << axisColor.red() << " " <<
            axisColor.green() << " " <<
            axisColor.blue() << endl;
    out << "    photons = " << photonColor.red() << " " <<
            photonColor.green() << " " <<
            photonColor.blue() << endl;
    out << "    electrons = " << electronColor.red() << " " <<
            electronColor.green() << " " <<
            electronColor.blue() << endl;
    out << "    positrons = " << positronColor.red() << " " <<
            positronColor.green() << " " <<
            positronColor.blue() << endl;
    out << "    energy scaling = " << energyScaling << endl;
    out << ":stop colors:" << endl;
}

void GeometryViewControl::loadConfig() {
    // Prompt the user to select a previous config file
    QFileInfo inputFileInfo = QFileInfo(filename);
    QString configFilename = QFileDialog::getOpenFileName(this, "Select an egs_view settings file", inputFileInfo.canonicalPath(), "*.egsview");

    if (configFilename.isEmpty()) {
        return;
    }

    loadConfig(configFilename);
}

void GeometryViewControl::loadConfig(QString configFilename) {
    // Get the rendering parameters
    RenderParameters &rp = gview->pars;

    EGS_Input *input = new EGS_Input;
    if (configFilename.size() > 0) {
        if (input->setContentFromFile(configFilename.toLatin1().data())) {
            QMessageBox::critical(this,"Config file read error",
                "Failed to open the config file for reading.",QMessageBox::Ok,0,0);
            return;
        }
    }

    int err; // A variable to track errors

    // Load the image size
    EGS_Input *iImageSize = input->takeInputItem("image size");
    if(iImageSize) {
        err = iImageSize->getInput("nx",rp.nx);
        err = iImageSize->getInput("ny",rp.ny);

        gview->resize(rp.nx,rp.ny);

        delete iImageSize;
    }

    // Load the particle track options
    EGS_Input *iTracks = input->takeInputItem("tracks");
    if(iTracks) {
        int show;
        err = iTracks->getInput("show tracks",show);
        if(!err) {
            if(show) {
                showTracksCheckbox->setCheckState(Qt::Checked);
            } else {
                showTracksCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        err = iTracks->getInput("photons",show);
        if(!err) {
            if(show) {
                showPhotonsCheckbox->setCheckState(Qt::Checked);
            } else {
                showPhotonsCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        err = iTracks->getInput("electrons",show);
        if(!err) {
            if(show) {
                showElectronsCheckbox->setCheckState(Qt::Checked);
            } else {
                showElectronsCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        err = iTracks->getInput("positrons",show);
        if(!err) {
            if(show) {
                showPositronsCheckbox->setCheckState(Qt::Checked);
            } else {
                showPositronsCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        delete iTracks;
    }

    // Load the overlay options
    EGS_Input *iOverlay = input->takeInputItem("overlay");
    if(iOverlay) {
        int show;
        err = iOverlay->getInput("show axis",show);
        if(!err) {
            if(show) {
                showAxesCheckbox->setCheckState(Qt::Checked);
            } else {
                showAxesCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        err = iOverlay->getInput("show axis labels",show);
        if(!err) {
            if(show) {
                showAxesLabelsCheckbox->setCheckState(Qt::Checked);
            } else {
                showAxesLabelsCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        err = iOverlay->getInput("show regions",show);
        if(!err) {
            if(show) {
                showRegionsCheckbox->setCheckState(Qt::Checked);
            } else {
                showRegionsCheckbox->setCheckState(Qt::Unchecked);
            }
        }
    }

    // Load camera view
    EGS_Input *iView = input->takeInputItem("camera view");
    if(iView) {
        // Get the camera position
        vector<EGS_Float> look;
        err = iView->getInput("camera position",look);
        if(!err) {
            look_at.x = look[0];
            look_at.y = look[1];
            look_at.z = look[2];

            setLookAtLineEdit();
            updateLookAtLineEdit();
            setLookAt();
        }

        // Get the camera orientation
        vector<EGS_Float> cam, camv1, camv2;
        err = iView->getInput("camera",cam);
        if(!err) {
            err = iView->getInput("camera v1",camv1);
            if(!err) {
                err = iView->getInput("camera v2",camv2);
                if(!err) {
                    camera = EGS_Vector(cam[0],cam[1],cam[2]);
                    camera_v1 = EGS_Vector(camv1[0],camv1[1],camv1[2]);
                    camera_v2 = EGS_Vector(camv2[0],camv2[1],camv2[2]);
                }
            }
        }

        err = iView->getInput("zoom",zoomlevel);
        setCameraPosition();
    }

    // Load home view
    EGS_Input *iHome = input->takeInputItem("home view");
    if(iHome) {
        // Get the home position
        vector<EGS_Float> look;
        err = iHome->getInput("home position",look);
        if(!err) {
            look_at_home.x = look[0];
            look_at_home.y = look[1];
            look_at_home.z = look[2];
        }

        // Get the home orientation
        vector<EGS_Float> cam, camv1, camv2;
        err = iHome->getInput("home",cam);
        if(!err) {
            err = iHome->getInput("home v1",camv1);
            if(!err) {
                err = iHome->getInput("home v2",camv2);
                if(!err) {
                    camera_home = EGS_Vector(cam[0],cam[1],cam[2]);
                    camera_home_v1 = EGS_Vector(camv1[0],camv1[1],camv1[2]);
                    camera_home_v2 = EGS_Vector(camv2[0],camv2[1],camv2[2]);
                }
            }
        }

        err = iHome->getInput("zoom",zoomlevel_home);
    }

    // Load the media colors
    EGS_Input *iMatColors = input->takeInputItem("material colors");
    if(iMatColors) {
        while(iMatColors->getInputItem("material")) {
            EGS_Input *iMat = iMatColors->takeInputItem("material");
            if(!iMat) {
                break;
            }

            string material;
            vector<int> rgb;
            int alpha;

            err = iMat->getInput("material",material);
            if(err) {
                continue;
            }

            // Find the index of the material by the same name
            int imed;
            if(material == "vacuum") {
                imed = nmed;
            } else {
                for(imed = 0; imed<nmed; ++imed) {
                    if(g->getMediumName(imed) == material) {
                        break;
                    }
                }
            }

            err = iMat->getInput("rgb",rgb);
            if(err || rgb.size() < 3) {
                continue;
            }
            err = iMat->getInput("alpha",alpha);
            if(err) {
                continue;
            }

            m_colors[imed] = qRgba(rgb[0], rgb[1], rgb[2], alpha);

            // Update the transparency bar
            if(imed == 0) {
                transparency->setValue(alpha);
            }
        }
        delete iMatColors;

        // Update swatches in the combo box
        QPixmap pixmap(10,10);
        for (int j=0; j<=nmed; j++) {
            pixmap.fill(m_colors[j]);
            materialCB->setItemIcon(j,pixmap);
        }
    }

    // Load the clipping planes
    EGS_Input *iClip = input->takeInputItem("clipping planes");
    if(iClip) {
        for (int i=0; i<cplanes->numPlanes(); i++) {
            EGS_Input *iPlane = iClip->takeInputItem("plane");
            if(!iPlane) {
                break;
            }

            EGS_Float ax, ay, az, d;
            int check;

            err = iPlane->getInput("ax",ax);
            if(!err) {
                cplanes->setCell(i,0,ax);
            } else {
                cplanes->clearCell(i,0);
            }

            err = iPlane->getInput("ay",ay);
            if(!err) {
                cplanes->setCell(i,1,ay);
            } else {
                cplanes->clearCell(i,1);
            }

            err = iPlane->getInput("az",az);
            if(!err) {
                cplanes->setCell(i,2,az);
            } else {
                cplanes->clearCell(i,2);
            }

            err = iPlane->getInput("d",d);
            if(!err) {
                cplanes->setCell(i,3,d);
            } else {
                cplanes->clearCell(i,3);
            }

            err = iPlane->getInput("applied",check);
            if(!err) {
                Qt::CheckState isChecked;
                if(check == Qt::Checked) {
                    isChecked = Qt::Checked;
                } else {
                    isChecked = Qt::Unchecked;
                }

                cplanes->setCell(i,4,isChecked);
            } else {
                cplanes->setCell(i,4,Qt::Checked);
            }

            delete iPlane;
        }

        delete iClip;
    }

    if(allowRegionSelection) {
        updateRegionTable();
    }

    // Load the hidden regions
    EGS_Input *iReg = input->takeInputItem("hidden regions");
    if(iReg) {
        vector<int> regionList;
        err = iReg->getInput("region list",regionList);
        // For every region in the table, check to see if it
        // is listed in the hidden regions list.
        // If so, hide it; if not, show it
        for(size_t i = 0; i < show_regions.size(); ++i) {
            QTableWidgetItem *itemRegion = regionTable->item(i,0);
            if(!itemRegion) {
                continue;
            }

            int ireg = itemRegion->text().toInt();

            // Hide the listed regions, otherwise show them
            QTableWidgetItem *itemShow = regionTable->item(i,3);
            if(!itemShow) {
                continue;
            }
            if(std::find(regionList.begin(), regionList.end(), ireg) != regionList.end()) {
                itemShow->setCheckState(Qt::Unchecked);
                show_regions[i] = false;
            } else {
                itemShow->setCheckState(Qt::Checked);
                show_regions[i] = true;
            }
        }
    }

    EGS_Input *iColors = input->takeInputItem("colors");
    if(iColors) {
        vector<int> rgb;

        err = iColors->getInput("background",rgb);
        if(!err && rgb.size() >= 3) {
            backgroundColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        err = iColors->getInput("text",rgb);
        if(!err && rgb.size() >= 3) {
            textColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        err = iColors->getInput("axis",rgb);
        if(!err && rgb.size() >= 3) {
            axisColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        err = iColors->getInput("photons",rgb);
        if(!err && rgb.size() >= 3) {
            photonColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        err = iColors->getInput("electrons",rgb);
        if(!err && rgb.size() >= 3) {
            electronColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        err = iColors->getInput("positrons",rgb);
        if(!err && rgb.size() >= 3) {
            positronColor = QColor(rgb[0], rgb[1], rgb[2]);
        }

        int useScaling;
        err = iColors->getInput("energy scaling",useScaling);
        if(!err) {
            energyScaling = useScaling;
            if(energyScaling) {
                energyScalingCheckbox->setCheckState(Qt::Checked);
            } else {
                energyScalingCheckbox->setCheckState(Qt::Unchecked);
            }
        }

        initColorSwatches();

        delete iColors;
    }

    updateView(true);
}

void GeometryViewControl::setFilename(QString str) {
    filename = str;
}

void GeometryViewControl::setTracksFilename(QString str) {
    filename_tracks = str;
}

void GeometryViewControl::checkboxAxes(bool toggle) {
    this->showAxesLabelsCheckbox->setEnabled(toggle);
    showAxes = toggle;
    if (toggle==false) {
        showAxesLabels = false;
    }
    else {
        showAxesLabels = showAxesLabelsCheckbox->isChecked();
    }
    updateView();
}

void GeometryViewControl::checkboxAxesLabels(bool toggle) {
    showAxesLabels = toggle;
    updateView();
}

void GeometryViewControl::checkboxShowRegions(bool toggle) {
    gview->showRegions(toggle);
}

void GeometryViewControl::checkboxShowTracks(bool toggle) {
    showTracks = toggle;
    updateView();
}

void GeometryViewControl::cameraHome() {

    // reset camera to home position
    camera = camera_home;
    camera_v1 = camera_home_v1;
    camera_v2 = camera_home_v2;

    // reset look_at
    look_at = look_at_home;
    setLookAtLineEdit();
    updateLookAtLineEdit();

    // reset zoom level
    zoomlevel = zoomlevel_home;

    // debug information
#ifdef VIEW_DEBUG
    egsWarning("In cameraHome()\n");
#endif

    // render
    setCameraPosition();
}

void GeometryViewControl::cameraOnAxis(char axis) {

    // set look_at point to the center of the object
    look_at = center;

    // get the distance to the look_at point
    EGS_Vector v0(look_at-camera);
    EGS_Float dist = v0.length();

    // put the camera on required axis
    if (axis=='x') {
        camera    = EGS_Vector(center.x+dist,center.y,center.z);
        camera_v1 = EGS_Vector(0,1,0);
        camera_v2 = EGS_Vector(0,0,1);
    }
    else if (axis=='X') {
        camera    = EGS_Vector(center.x-dist,center.y,center.z);
        camera_v1 = EGS_Vector(0,-1,0);
        camera_v2 = EGS_Vector(0,0,1);
    }
    else if (axis=='y') {
        camera    = EGS_Vector(center.x,center.y+dist,center.z);
        camera_v1 = EGS_Vector(-1,0,0);
        camera_v2 = EGS_Vector(0,0,1);
    }
    else if (axis=='Y') {
        camera    = EGS_Vector(center.x,center.y-dist,center.z);
        camera_v1 = EGS_Vector(1,0,0);
        camera_v2 = EGS_Vector(0,0,1);
    }
    else if (axis=='z') {
        camera    = EGS_Vector(center.x,center.y,center.z+dist);
        camera_v1 = EGS_Vector(1,0,0);
        camera_v2 = EGS_Vector(0,1,0);
    }
    else if (axis=='Z') {
        camera    = EGS_Vector(center.x,center.y,center.z-dist);
        camera_v1 = EGS_Vector(1,0,0);
        camera_v2 = EGS_Vector(0,-1,0);
    }

    // debug information
#ifdef VIEW_DEBUG
    egsWarning("In cameraOnAxis()\n");
#endif

    // render
    setCameraPosition();
}

// slots for camera view buttons
void GeometryViewControl::camera_x()  {
    cameraOnAxis('x');
}
void GeometryViewControl::camera_y()  {
    cameraOnAxis('y');
}
void GeometryViewControl::camera_z()  {
    cameraOnAxis('z');
}
void GeometryViewControl::camera_mx() {
    cameraOnAxis('X');
}
void GeometryViewControl::camera_my() {
    cameraOnAxis('Y');
}
void GeometryViewControl::camera_mz() {
    cameraOnAxis('Z');
}

void GeometryViewControl::cameraHomeDefine() {

    // reset camera to home position
    camera_home = camera;
    camera_home_v1 = camera_v1;
    camera_home_v2 = camera_v2;
    look_at_home = look_at;
    zoomlevel_home = zoomlevel;

    // debug information
#ifdef VIEW_DEBUG
    egsWarning("In cameraHomeDefine()\n");
#endif
}


void GeometryViewControl::cameraTranslate(int dx, int dy) {

    // distance from camera to aim point
    EGS_Float r = (camera-look_at).length();

    // Set a scaling factor for the camera translation so that
    // it goes slower at higher zoom levels
    EGS_Float scale;
    if(zoomlevel > -10) {
        scale = pow(1./(zoomlevel+110),1.5);
    } else {
        scale = 0.001;
    }

    // compute displacement (in world units)
    EGS_Float tx = -r*dx*scale;
    EGS_Float ty = r*dy*scale;

    // translation
    camera  = camera  + camera_v2*ty + camera_v1*tx;
    look_at = look_at + camera_v2*ty + camera_v1*tx;

    // debug information
#ifdef VIEW_DEBUG
    egsWarning("In cameraTranslate()\n");
#endif

    // adjust look_at lineEdit
    setLookAtLineEdit();
    updateLookAtLineEdit();

    // render
    setCameraPosition();
}


void GeometryViewControl::cameraRotate(int dx, int dy) {

    // camera position vector and camera distance
    EGS_Vector v0 = camera - look_at;
    EGS_Float r = v0.length();

    // Set a scaling factor for the camera rotation so that
    // it goes slower at higher zoom levels
    // Note: the minimum zoomlevel is -400 and does become positive
    EGS_Float scale;
    if(zoomlevel > -97) {
        scale = pow(1./(zoomlevel+110),1.2);
    } else {
        scale = 0.05;
    }

    // new position of camera
    v0 = v0 + (camera_v2*dy + camera_v1*-dx)*scale*r;
    v0.normalize();
    camera = look_at + v0*r;

    // camera aim vector (unit vector pointing at look_at point)
    v0 *= -1;

    // new up and side vectors, v1 and v2
    camera_v1 = v0.times(camera_v2);
    camera_v1.normalize();
    camera_v2 = camera_v1.times(v0);
    camera_v2.normalize();

    // render
    setCameraPosition();
}

void GeometryViewControl::cameraRoll(int dx) {

    // camera aim vector
    EGS_Vector v0 = look_at - camera;
    v0.normalize();

    // new up vector
    camera_v1 = camera_v1 - camera_v2 * 0.1 * -dx;
    camera_v1.normalize();
    camera_v2 = camera_v1.times(v0);
    camera_v2.normalize();

    // render
    setCameraPosition();
}

void GeometryViewControl::cameraZoom(int dy) {
    zoomlevel -= dy;
    if (zoomlevel < -400) {
        zoomlevel = -400;
    }
// debug information
#ifdef VIEW_DEBUG
    egsWarning("In cameraZoom(%d)\n", dy);
    egsWarning(" new zoomlevel = %d\n", zoomlevel);
    egsWarning(" size = %g\n", size);
#endif

// render
    updateView();
}

void GeometryViewControl::thetaRotation(int Theta) {
#ifdef VIEW_DEBUG
    egsWarning("In thetaRotation(%d)\n",Theta);
#endif
    theta = Theta*M_PI/180;
    c_theta = cos(theta);
    s_theta = sin(theta);
    setCameraPosition();
}


void GeometryViewControl::phiRotation(int Phi) {
#ifdef VIEW_DEBUG
    egsWarning("In phiRotation(%d)\n",Phi);
#endif
    phi = Phi*M_PI/180;
    c_phi = cos(phi);
    s_phi = sin(phi);
    setCameraPosition();
}

void GeometryViewControl::setCameraPosition() {

    screen_xo = look_at - (camera-look_at)*(1/3.);
//  camera    = look_at + EGS_Vector(s_theta*s_phi,s_theta*c_phi,c_theta)*3*distance;
//  screen_xo = look_at - EGS_Vector(s_theta*s_phi,s_theta*c_phi,c_theta)*distance;

#ifdef VIEW_DEBUG
    egsWarning("camera: (%g,%g,%g) screen: (%g,%g,%g)\n",camera.x,camera.y,camera.z,screen_xo.x,screen_xo.y,screen_xo.z);
#endif

    screen_v1 = camera_v1;
    screen_v2 = camera_v2;
    if (moveLight->isChecked()) {
        p_light = camera;
        setLightLineEdit();
    }
    updateView();
}

void GeometryViewControl::changeAmbientLight(int alight) {
#ifdef VIEW_DEBUG
    egsWarning("In changeAmbientLight(%d)\n",alight);
#endif
    a_light = alight;
    updateView(true);
}


void GeometryViewControl::changeTransparency(int t) {
    int med = materialCB->currentIndex();
    QRgb c = m_colors[med];
    m_colors[med] = qRgba(qRed(c), qGreen(c), qBlue(c), t);
#ifdef VIEW_DEBUG
    egsWarning("In changeTransparency(%d): set color to %d\n",t,m_colors[med]);
#endif
    updateView(true);
}


void GeometryViewControl::moveLightChanged(int toggle) {
#ifdef VIEW_DEBUG
    egsWarning("In moveLightChanged(%d)\n",toggle);
#endif
    if (!toggle) {
        lightPosGroup->setEnabled(true);
    }
    else {
        lightPosGroup->setEnabled(false);
    }
}


void GeometryViewControl::setLightPosition() {
#ifdef VIEW_DEBUG
    egsWarning("In setLightPosition()\n");
#endif
    bool ok_x, ok_y, ok_z;
    EGS_Float xx = lightX->text().toDouble(&ok_x),
              yy = lightY->text().toDouble(&ok_y),
              zz = lightZ->text().toDouble(&ok_z);
    if (ok_x) {
        p_light.x = xx;
    }
    else {
        lightX->setText(QString("%1").arg((double)p_light.x,0,'g',4));
    }
    if (ok_y) {
        p_light.y = yy;
    }
    else {
        lightY->setText(QString("%1").arg((double)p_light.y,0,'g',4));
    }
    if (ok_z) {
        p_light.z = zz;
    }
    else {
        lightZ->setText(QString("%1").arg((double)p_light.z,0,'g',4));
    }
    if (ok_x || ok_y || ok_z) {
        updateView();
    }
}


void GeometryViewControl::setLookAt() {
#ifdef VIEW_DEBUG
    egsWarning("In setLookAt()\n");
#endif
    bool ok_x, ok_y, ok_z;
    EGS_Float xx = lookX->text().toDouble(&ok_x),
              yy = lookY->text().toDouble(&ok_y),
              zz = lookZ->text().toDouble(&ok_z);
    if (ok_x) {
        look_at.x = xx;
    }
    else {
        lookX->setText(QString("%1").arg((double)look_at.x,0,'g',4));
    }
    if (ok_y) {
        look_at.y = yy;
    }
    else {
        lookY->setText(QString("%1").arg((double)look_at.y,0,'g',4));
    }
    if (ok_z) {
        look_at.z = zz;
    }
    else {
        lookZ->setText(QString("%1").arg((double)look_at.z,0,'g',4));
    }
    if (ok_x || ok_y || ok_z) {

        // camera pointing vector
        EGS_Vector v0 = look_at-camera;
        v0.normalize();

        // new up and side vectors, v1 and v2
        camera_v1 = v0.times(camera_v2);
        camera_v1.normalize();
        camera_v2 = camera_v1.times(v0);
        camera_v2.normalize();

        // update camera position
        setCameraPosition();
    }
}

void GeometryViewControl::loadTracksDialog() {
#ifdef VIEW_DEBUG
    egsWarning("In loadTracksDialog()\n");
#endif

    QFileInfo inputFileInfo = QFileInfo(filename);
    filename_tracks = QFileDialog::getOpenFileName(this, "Select particle tracks file", inputFileInfo.canonicalPath(), "*.ptracks");

    if (filename_tracks.isEmpty()) {
        return;
    }

    gview->loadTracks(filename_tracks);
    updateView();
}

void GeometryViewControl::viewAllMaterials() {
#ifdef VIEW_DEBUG
    egsWarning("In viewAllMaterials()\n");
#endif
}

void GeometryViewControl::reportViewSettings(int x,int y) {
    // convert mouse coordinates to screen coordinate
    int w=gview->width();
    int h=gview->height();
    EGS_Float xscreen, yscreen;
    EGS_Float projection_m = size * pow(0.5, zoomlevel / 48.);
    EGS_Float xscale = w > h ? projection_m * w / h : projection_m;
    EGS_Float yscale = h > w ? projection_m * h / w : projection_m;
    xscreen = (x-w/2)*xscale/w;
    yscreen = -(y-h/2)*yscale/h;
    EGS_Vector xp(screen_xo + screen_v2*yscreen + screen_v1*xscreen);
    egsWarning("In reportViewSettings(%d,%d): xp=(%g,%g,%g)\n",x,y,xp.x,xp.y,xp.z);
    EGS_Vector u(xp-camera);
    u.normalize();
    egsWarning(" camera=(%15.10f,%15.10f,%15.10f), u=(%14.10f,%14.10f,%14.10f)\n",camera.x,camera.y,camera.z,u.x,u.y,u.z);
}

void GeometryViewControl::setRotationPoint(EGS_Vector hitCoord) {
    look_at = hitCoord;
    setLookAtLineEdit();
    updateLookAtLineEdit();
    setLookAt();
}

void GeometryViewControl::quitApplication() {
    delete gview;
    close();
}

void GeometryViewControl::setProjectionLineEdit() {
}

void GeometryViewControl::setLightLineEdit() {
    lightX->setText(QString("%1").arg((double)p_light.x,0,'g',4));
    lightY->setText(QString("%1").arg((double)p_light.y,0,'g',4));
    lightZ->setText(QString("%1").arg((double)p_light.z,0,'g',4));
}

void GeometryViewControl::setLookAtLineEdit() {
    lookX->setText(QString("%1").arg((double)look_at.x,0,'g',4));
    lookY->setText(QString("%1").arg((double)look_at.y,0,'g',4));
    lookZ->setText(QString("%1").arg((double)look_at.z,0,'g',4));
}

void GeometryViewControl::updateLookAtLineEdit() {
    lookX->repaint();
    lookY->repaint();
    lookZ->repaint();
}

int GeometryViewControl::setGeometry(
    EGS_BaseGeometry *geom,
    const std::vector<EGS_UserColor> &ucolors,
    EGS_Float xmin, EGS_Float xmax, EGS_Float ymin, EGS_Float ymax,
    EGS_Float zmin, EGS_Float zmax, bool justReloading) {
    if (!geom) {
        egsWarning("setGeometry(): got null geometry\n");
        return 1;
    }
    g = geom;

    if (!filename_tracks.isEmpty()) {
        gview->loadTracks(filename_tracks);
    }

#ifdef VIEW_DEBUG
    qDebug("Got %d user defined colors",int(ucolors.size()));
#endif

    // first save user's current material settings if reloading
    int nSave = materialCB->count();
    QRgb *saveColors  = new QRgb[nSave];
    QString *saveName = new QString[nSave];
    QString saveCurrent = materialCB->currentText();
    if (justReloading && m_colors) {
        for (int i=0; i<nSave; i++) {
            saveColors[i] = m_colors[i];
            saveName[i]   = materialCB->itemText(i);
        }
    }

    // delete m_colors
    if (m_colors && nmed > 0) {
        delete [] m_colors;
    }

#ifdef VIEW_DEBUG
    egsWarning("In setGeometry(), geometry name is %s\n",g->getName().c_str());
#endif

    // get number of media from geometry
    nmed = g->nMedia();
    if (nmed < 1) {
        QMessageBox::critical(this,"Geometry error",
                              "The geometry defines no media",QMessageBox::Ok,0,0);
        return 1;
    }

    // set up material combo box items
    materialCB->clear();
    m_colors = new QRgb [nmed+1]; // nmed+1 for vacuum
    for (int j=0; j<nmed; j++) {
        materialCB->insertItem(j,g->getMediumName(j));
    }

    // Always insert vacuum since it is not in the medium list
    // The only way to check if it was in geometry would be to loop through
    // every region and check if g->medium(ireg)==-1
    materialCB->insertItem(nmed,"vacuum");

    int nstandard = sizeof(standard_red)/sizeof(unsigned char);
    int js = 0;
    for (int j=0; j<=nmed; j++) {
        string med_name;
        if(j == nmed) {
            med_name = "vacuum";
        } else {
            med_name = g->getMediumName(j);
        }
        unsigned int i;
        for (i=0; i<ucolors.size(); ++i) if (med_name == ucolors[i].medname) {
                break;
            }
        if (i < ucolors.size()) {
            m_colors[j] = qRgba(ucolors[i].red,ucolors[i].green,ucolors[i].blue,ucolors[i].alpha);
        }
        else {
            if(j == nmed) {
                // Vacuum defaults to black, transparent
                m_colors[j] = qRgba(0,0,0,0);
            } else {
                m_colors[j] = qRgba(standard_red[js], standard_green[js], standard_blue[js], 255);
            }
            if ((++js) >= nstandard) {
                js = 0;
            }
        }
    }

    /*
    if( nmed <= nstandard ) {
        for(int j=0; j<nmed; j++) m_colors[j] = qRgba(standard_red[j], standard_green[j], standard_blue[j], 255);
    }
    else {
        egsWarning(" too many colors\n"); // TODO
        for(int j=0; j<nmed; j++) {
            int jj = j%nstandard;
            m_colors[j] = qRgba(standard_red[jj], standard_green[jj], standard_blue[jj], 255);
        }
    }
    */

    // copy user's saved setting (for media names that were defined before the reload)
    materialCB->setCurrentIndex(0);
    if (justReloading) {
        for (int j=0; j<=nmed; j++) {
            for (int k=0; k<nSave; k++) {
                if (materialCB->itemText(j) == saveName[k]) {
                    m_colors[j] = saveColors[k];
                }
            }
            if (materialCB->itemText(j) == saveCurrent) {
                materialCB->setCurrentIndex(j);
            }
        }
    }
    // add swatches in the combo box
    QPixmap pixmap(10,10);
    {
        for (int j=0; j<=nmed; j++) {
            pixmap.fill(m_colors[j]);
            materialCB->setItemIcon(j,pixmap);
        }
    }

    // clean up saved settings
    delete [] saveColors;
    delete [] saveName;

    QProgressDialog progress("Analyzing geometry...","&Cancel",0,130,this);
    progress.setObjectName("progress");
    progress.setMinimumDuration(500);
    EGS_Float dx = (xmax-xmin)/100, dy = (ymax-ymin)/100, dz = (zmax-zmin)/100;
    bool found = false;
    EGS_Vector xo(0,0,0);
    int ireg = g->isWhere(xo);
    // egsInformation("for (0,0,0) ireg=%d\n",ireg);
    if (ireg >= 0) {
        found = true;
        progress.setValue(100);
    }
    else {
        progress.setMaximum(130);
        // look for an inside point.
        for (int k=0; k<100; k++) {
            progress.setValue(k);
            qApp->processEvents();
            if (progress.wasCanceled()) {
                return 2;
            }
            xo.z = zmin + dz*(k+0.5);
            for (int i=0; i<100; i++) {
                xo.x = xmin + dx*(i+0.5);
                for (int j=0; j<100; j++) {
                    xo.y = ymin + dy*(j+0.5);
                    ireg = g->isWhere(xo);
                    if (ireg >= 0) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (!found) {
            progress.setValue(132);
            qApp->processEvents();
            QMessageBox::critical(this,"Geometry error",
                                  "Failed to find a point that is inside the geometry",
                                  QMessageBox::Ok,0,0);
            return 3;
        }
        progress.setValue(100);
        qApp->processEvents();
        if (progress.wasCanceled()) {
            return 2;
        }
#ifdef VIEW_DEBUG
        egsWarning("found xo = (%g,%g,%g)\n",xo.x,xo.y,xo.z);
#endif
    }

    EGS_Vector pmin(veryFar,veryFar,veryFar), pmax(-veryFar,-veryFar,-veryFar);
    //egsWarning("xo = (%g,%g,%g)\n",xo.x,xo.y,xo.z);
    for (int isize=0; isize<6; isize++) {
        progress.setValue(100+5*isize);
        qApp->processEvents();
        if (progress.wasCanceled()) {
            return 2;
        }
        //egsWarning("================ side %d\n",isize);
        int max_step = g->getMaxStep();
        for (int i=0; i<100; i++) {
            for (int j=0; j<100; j++) {
                EGS_Vector u;
                if (isize == 0) {
                    u = EGS_Vector(xmin+dx*i,ymin+dy*j,zmin);
                }
                else if (isize == 1) {
                    u = EGS_Vector(xmin+dx*i,ymin+dy*j,zmax);
                }
                else if (isize == 2) {
                    u = EGS_Vector(xmin,ymin+dy*j,zmin+dz*i);
                }
                else if (isize == 3) {
                    u = EGS_Vector(xmax,ymin+dy*j,zmin+dz*i);
                }
                else if (isize == 4) {
                    u = EGS_Vector(xmin+dx*i,ymin,zmin+dz*j);
                }
                else {
                    u = EGS_Vector(xmin+dx*i,ymax,zmin+dz*j);
                }
                u -= xo;
                u.normalize();
                EGS_Vector x(xo);
                int ir = ireg;
                int nstep = 0;
                do {
                    EGS_Float t = veryFar;
                    int inew = g->howfar(ir,x,u,t);
                    if (inew == ir) {
                        break;
                    }
                    if (++nstep > max_step) {
                        if (nstep == max_step+1) {
                            egsWarning("\nMore than %d steps through geometry?\n",max_step);
                            egsWarning("u = (%g,%g,%g)\n",u.x,u.y,u.z);
                        }
                        egsWarning("ir=%d inew=%d x=(%g,%g,%g) t=%g\n",ir,inew,x.x,x.y,x.z,t);
                        if (nstep > max_step + 50) {
                            egsWarning("\nToo many steps, ignoring ray\n");
                            x = xo;
                            break;
                        }
                    }
                    x += u*t;
                    ir = inew;
                }
                while (ir >= 0);
                //egsWarning("u = (%g,%g,%g) xf = (%g,%g,%g)\n",u.x,u.y,u.z,
                //        x.x,x.y,x.z);
                if (x.x < pmin.x) {
                    pmin.x = x.x;
                }
                if (x.x > pmax.x) {
                    pmax.x = x.x;
                }
                if (x.y < pmin.y) {
                    pmin.y = x.y;
                }
                if (x.y > pmax.y) {
                    pmax.y = x.y;
                }
                if (x.z < pmin.z) {
                    pmin.z = x.z;
                }
                if (x.z > pmax.z) {
                    pmax.z = x.z;
                }
            }
        }
    }
    center = (pmin + pmax)*0.5;
    if (fabs(center.x) < 0.001) {
        center.x = 0;
    }
    if (fabs(center.y) < 0.001) {
        center.y = 0;
    }
    if (fabs(center.z) < 0.001) {
        center.z = 0;
    }
#ifdef VIEW_DEBUG
    egsWarning(" center: (%g,%g,%g)\n",center.x,center.y,center.z);
    egsWarning(" xmin: (%g,%g,%g)\n",pmin.x,pmin.y,pmin.z);
    egsWarning(" xmax: (%g,%g,%g)\n",pmax.x,pmax.y,pmax.z);
#endif
    EGS_Float xsize = (pmax.x - pmin.x)/2;
    EGS_Float ysize = (pmax.y - pmin.y)/2;
    EGS_Float zsize = (pmax.z - pmin.z)/2;
    size = xsize;
    if (ysize > size) {
        size = ysize;
    }
    if (zsize > size) {
        size = zsize;
    }
    axesmax  = pmax + EGS_Vector(size, size, size)*0.3;

    if (!justReloading) {
        distance = size*3.5; // ~2*sqrt(3);
        look_at = center;
        look_at_home = look_at;
        setLookAtLineEdit();
        if (distance > 60000) {
            egsWarning("too big: %g\n",size);
            distance = 9999;
        }
        else {
            zoomlevel = -112;
        }
        setProjectionLineEdit();
        //p_light = look_at+EGS_Vector(s_theta*s_phi,s_theta*s_phi,c_theta)*distance;
        p_light = look_at+EGS_Vector(s_theta*s_phi,s_theta*s_phi,c_theta)*3*distance;
        setLightLineEdit();
        camera = p_light;
        screen_xo = look_at-EGS_Vector(s_theta*s_phi,s_theta*s_phi,c_theta)*distance;
        screen_v1 = EGS_Vector(c_phi,-s_phi,0);
        screen_v2 = EGS_Vector(c_theta*s_phi,c_theta*c_phi,-s_theta);

        // camera orientation vectors (same as the screen vectors)
        camera_v1 = screen_v1;
        camera_v2 = screen_v2;

        // save camera home position
        camera_home = camera;
        camera_home_v1 = camera_v1;
        camera_home_v2 = camera_v2;
        zoomlevel_home = zoomlevel;
    }

    updateView();

    return 0;
}

void GeometryViewControl::updateView(bool transform) {
    // transfer
    RenderParameters &rp = gview->pars;
    rp.axesmax = axesmax;
    rp.camera = camera;
    rp.camera_v1 = camera_v1;
    rp.camera_v2 = camera_v2;
    //r.clipping_planes
    rp.draw_axes = showAxes;
    rp.draw_axeslabels = showAxesLabels;
    rp.draw_tracks = showTracks;
    float a = 0.01 * a_light;
    rp.global_ambient_light = EGS_Vector(a,a,a);

    rp.lights = vector<EGS_Light>();
    rp.lights.push_back(EGS_Light(p_light, EGS_Vector(1,1,1)));

    rp.material_colors = vector<EGS_MaterialColor>();
    for (int j=0; j<=nmed; j++) {
        EGS_Float r = ((EGS_Float) qRed(m_colors[j]))/255.;
        EGS_Float gr = ((EGS_Float) qGreen(m_colors[j]))/255.;
        EGS_Float b = ((EGS_Float) qBlue(m_colors[j]))/255.;
        EGS_Float alpha = ((EGS_Float) qAlpha(m_colors[j]))/255.;
        rp.material_colors.push_back(EGS_MaterialColor(EGS_Vector(r,gr,b),alpha));
    }

    rp.clipping_planes = vector<EGS_ClippingPlane>();
    for (int j=0; j<cplanes->numPlanes(); j++) {
        EGS_Vector a;
        EGS_Float d;
        if (cplanes->getPlane(j,a,d)) {
            rp.clipping_planes.push_back(EGS_ClippingPlane(a,d));
        }
    }

    rp.displayColors = vector<EGS_Vector>(6);
    rp.displayColors[0] = EGS_Vector(backgroundColor.red()/255., backgroundColor.green()/255., backgroundColor.blue()/255.);
    rp.displayColors[1] = EGS_Vector(textColor.red()/255., textColor.green()/255., textColor.blue()/255.);
    rp.displayColors[2] = EGS_Vector(axisColor.red()/255., axisColor.green()/255., axisColor.blue()/255.);
    rp.displayColors[3] = EGS_Vector(photonColor.red()/255., photonColor.green()/255., photonColor.blue()/255.);
    rp.displayColors[4] = EGS_Vector(electronColor.red()/255., electronColor.green()/255., electronColor.blue()/255.);
    rp.displayColors[5] = EGS_Vector(positronColor.red()/255., positronColor.green()/255., positronColor.blue()/255.);

    rp.energyScaling = energyScaling;

    rp.projection_m = size * pow(0.5, zoomlevel / 48.);
    rp.screen_v1 = screen_v1;
    rp.screen_v2 = screen_v2;
    rp.screen_xo = screen_xo;
    rp.show_electrons = showElectronTracks;
    rp.show_photons = showPhotonTracks;
    rp.show_positrons = showPositronTracks;
    rp.size = size;
    rp.show_regions = show_regions;

    gview->render(g, transform);
}

void GeometryViewControl::updateColorLabel(int med) {
    transparency->setValue(qAlpha(m_colors[med]));
}

void GeometryViewControl::changeColor() {
#ifdef VIEW_DEBUG
    egsWarning("In changeColor()\n");
    egsWarning(" widget size = %d %d\n",width(),height());
#endif
    int med = materialCB->currentIndex();
    bool ok;
    QRgb newc = QColorDialog::getRgba(m_colors[med],&ok,this);
    if (ok) {
        m_colors[med] = newc;
        QPixmap pixmap(10,10);
        pixmap.fill(m_colors[med]);
        materialCB->setItemIcon(med, pixmap);
        transparency->setValue(qAlpha(newc));
        if(allowRegionSelection) {
            updateRegionTable(med);
        }
        updateView();
    }
}

void GeometryViewControl::initColorSwatches() {
    QPixmap pixmap(10,10);

    pixmap.fill(backgroundColor);
    cBackgroundButton->setIcon(pixmap);

    pixmap.fill(textColor);
    cTextButton->setIcon(pixmap);

    pixmap.fill(axisColor);
    cAxisButton->setIcon(pixmap);

    pixmap.fill(photonColor);
    cPhotonsButton->setIcon(pixmap);

    pixmap.fill(electronColor);
    cElectronsButton->setIcon(pixmap);

    pixmap.fill(positronColor);
    cPositronsButton->setIcon(pixmap);
}

void GeometryViewControl::setBackgroundColor() {
    QColor newc = QColorDialog::getColor(backgroundColor,this);
    if (newc.isValid()) {
        backgroundColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(backgroundColor);
        cBackgroundButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setTextColor() {
    QColor newc = QColorDialog::getColor(textColor,this);
    if (newc.isValid()) {
        textColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(textColor);
        cTextButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setAxisColor() {
    QColor newc = QColorDialog::getColor(axisColor,this);
    if (newc.isValid()) {
        axisColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(axisColor);
        cAxisButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setPhotonColor() {
    QColor newc = QColorDialog::getColor(photonColor,this);
    if (newc.isValid()) {
        photonColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(photonColor);
        cPhotonsButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setElectronColor() {
    QColor newc = QColorDialog::getColor(electronColor,this);
    if (newc.isValid()) {
        electronColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(electronColor);
        cElectronsButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setPositronColor() {
    QColor newc = QColorDialog::getColor(positronColor,this);
    if (newc.isValid()) {
        positronColor = newc;

        // Set the color swatch in the button
        QPixmap pixmap(10,10);
        pixmap.fill(positronColor);
        cPositronsButton->setIcon(pixmap);

        updateView();
    }
}

void GeometryViewControl::setEnergyScaling(bool toggle) {
    energyScaling = toggle;
    updateView();
}

void GeometryViewControl::saveImage() {
    int res = save_image->exec();
#ifdef VIEW_DEBUG
    egsWarning("GeometryViewControl::saveImage(): got %d\n",res);
#endif
    if (!res) {
        return;
    }
    int nx, ny;
    save_image->getImageSize(&nx,&ny);
    QString format = save_image->getImageFormat();
    QString fname = save_image->getImageFileName();
#ifdef VIEW_DEBUG
    egsWarning("\nAbout to save %dx%d image into file %s in format %s\n\n",
               nx,ny,fname.toUtf8().constData(),format.toUtf8().constData());
#endif
    // Disable save button until image save complete
    gview->saveView(g,nx,ny,fname,format);
}

void GeometryViewControl::setClippingPlanes() {
#ifdef VIEW_DEBUG
    egsWarning("In GeometryViewControl::setClippingPlanes():\n");
#endif
//    vis->clearClippingPlanes();
//    for(int j=0; j<cplanes->numPlanes(); j++) {
//        EGS_Vector a; EGS_Float d;
//        if( cplanes->getPlane(j,a,d) ) {
//#ifdef VIEW_DEBUG
//            egsWarning("  got plane (%g,%g,%g) %g\n",a.x,a.y,a.z,d);
//#endif
////            vis->addClippingPlane(a,d);
//        }
//    }
    updateView();
}

void GeometryViewControl::showPhotonsCheckbox_toggled(bool toggle) {
    showPhotonTracks = toggle;
    updateView();
}

void GeometryViewControl::showElectronsCheckbox_toggled(bool toggle) {
    showElectronTracks = toggle;
    updateView();
}


void GeometryViewControl::showPositronsCheckbox_toggled(bool toggle) {
    showPositronTracks = toggle;
    updateView();
}

void GeometryViewControl::startTransformation() {
    gview->startTransformation();
}

void GeometryViewControl::endTransformation() {
    gview->endTransformation();
}

void GeometryViewControl::updateRegionTable() {
    if (!g) {
        return;
    }

    // Adjust the table sizing
    regionTable->setColumnWidth(0,60);
    regionTable->setColumnWidth(1,28);
    regionTable->setColumnWidth(2,200);
    regionTable->setColumnWidth(3,28);

    int nreg = g->regions();
    show_regions.resize(nreg,true);

    // Count the number of real regions
    int nReal = 0;
    for(int ireg = 0; ireg < nreg; ++ireg) {
        if(g->isRealRegion(ireg)) {
            nReal++;
        }
    }

    // Set the number of rows to match the number of real regions
    regionTable->setRowCount(nReal);

    // Get the default checkbox background color
    QBrush checkboxColor = regionTable->item(0,3)->background();

    // Populate the table
    int i = 0;
    for(int ireg = 0; ireg < nreg; ++ireg) {
        if(g->isRealRegion(ireg)) {
            int imed = g->medium(ireg);

            // Set the region number
            QTableWidgetItem *regItem = regionTable->item(i,0);
            if(!regItem) {
                regItem = new QTableWidgetItem();
                regionTable->setItem(i,0,regItem);
            }
            regItem->setText(QString::number(ireg));

            // Set the material color
            QTableWidgetItem *colorItem = regionTable->item(i,1);
            if(!colorItem) {
                colorItem = new QTableWidgetItem();
                regionTable->setItem(i,1,colorItem);
            }
            if(imed < 0) {
                colorItem->setBackground(QBrush(QColor(m_colors[nmed])));
            } else {
                colorItem->setBackground(QBrush(QColor(m_colors[imed])));
            }
            colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsSelectable);

            // Set the material name
            QTableWidgetItem *matItem = regionTable->item(i,2);
            if(!matItem) {
                matItem = new QTableWidgetItem();
                regionTable->setItem(i,2,matItem);
            }
            if(imed < 0) {
                matItem->setText(QString("vacuum"));
            } else {
                matItem->setText(QString(g->getMediumName(imed)));
            }
            matItem->setFlags(matItem->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsSelectable);

            // Set the show/hide checkbox
            QTableWidgetItem *showItem = regionTable->item(i,3);
            if(!showItem) {
                showItem = new QTableWidgetItem();
                regionTable->setItem(i,3,showItem);
            }
            showItem->setCheckState(Qt::Checked);
            showItem->setBackground(checkboxColor);
            showItem->setFlags(showItem->flags() & ~Qt::ItemIsEditable & ~Qt::ItemIsSelectable);

            ++i;
        }
    }
}

// This is for just updating when a single material color is changed
void GeometryViewControl::updateRegionTable(int imedToChange) {
    if (!g) {
        return;
    }

    // Update the material color
    int i = 0;
    int nreg = g->regions();
    for(int ireg = 0; ireg < nreg; ++ireg) {
        if(g->isRealRegion(ireg)) {
            int imed = g->medium(ireg);

            if(imed < 0) {
                imed = nmed;
            }
            if(imed != imedToChange) {
                ++i;
                continue;
            }

            // Set the material color
            QTableWidgetItem *colorItem = regionTable->item(i,1);
            if(!colorItem) {
                colorItem = new QTableWidgetItem();
                regionTable->setItem(i,1,colorItem);
            }
            colorItem->setBackground(QBrush(QColor(m_colors[imed])));

            ++i;
        }
    }
}

void GeometryViewControl::toggleRegion(int i, int j) {
    if(!g) {
        return;
    }

    int ireg = -1;
    Qt::CheckState checked = Qt::Checked;

    QTableWidgetItem *item = regionTable->item(i,0);
    if(item) {
        ireg = item->text().toInt();
    }

    item = regionTable->item(i,3);
    if(item) {
        checked = item->checkState();
    }

    if(checked == Qt::Checked) {
        show_regions[ireg] = true;
    } else {
        show_regions[ireg] = false;
    }

    // If the region number was changed, update the color swatch and material
    if(j == 0) {
        int imed = g->medium(ireg);

        if(imed < 0) {
            imed = nmed;
        }

        // Set the material color
        QTableWidgetItem *colorItem = regionTable->item(i,1);
        if(!colorItem) {
            colorItem = new QTableWidgetItem();
            regionTable->setItem(i,1,colorItem);
        }
        colorItem->setBackground(QBrush(QColor(m_colors[imed])));

        // Set the material name
        QTableWidgetItem *matItem = regionTable->item(i,2);
        if(!matItem) {
            matItem = new QTableWidgetItem();
            regionTable->setItem(i,2,matItem);
        }
        matItem->setText(QString(g->getMediumName(imed)));
    }

    updateView();
}

void GeometryViewControl::showAllRegions() {
    if (!g) {
        return;
    }

    for(size_t i = 0; i < show_regions.size(); ++i) {

        // Set the show/hide checkbox
        QTableWidgetItem *showItem = regionTable->item(i,3);
        if(!showItem) {
            showItem = new QTableWidgetItem();
            regionTable->setItem(i,3,showItem);
        }
        showItem->setCheckState(Qt::Checked);
    }
}

void GeometryViewControl::hideAllRegions() {
    if (!g) {
        return;
    }

    for(size_t i = 0; i < show_regions.size(); ++i) {

        // Set the show/hide checkbox
        QTableWidgetItem *showItem = regionTable->item(i,3);
        if(!showItem) {
            showItem = new QTableWidgetItem();
            regionTable->setItem(i,3,showItem);
        }
        showItem->setCheckState(Qt::Unchecked);
    }
}

void GeometryViewControl::enlargeFont() {
    QFont new_font = this->font();
    new_font.setPointSize(new_font.pointSize() + 1);
    QApplication::setFont(new_font);

    QFont controlsFont = controlsText->font();
    QTextCursor cursor = controlsText->textCursor();
    controlsText->selectAll();
    controlsText->setFontPointSize(controlsFont.pointSize() + 1);
    controlsText->setTextCursor(cursor);
}

void GeometryViewControl::shrinkFont() {
    QFont new_font = this->font();
    if(new_font.pointSize() > 1) {
        new_font.setPointSize(new_font.pointSize() - 1);
        QApplication::setFont(new_font);
    }

    QFont controlsFont = controlsText->font();
    QTextCursor cursor = controlsText->textCursor();
    controlsText->selectAll();
    controlsText->setFontPointSize(controlsFont.pointSize() - 1);
    controlsText->setTextCursor(cursor);
}
