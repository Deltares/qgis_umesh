#include <stdlib.h>
#include <string.h>

#define DLL_EXPORT
#include "MyDrawingCanvas.h"
#include "map_time_manager_window.h"
#include "color_ramp.h"

#define GUI_EXPORT __declspec(dllimport)
#include "qgsmapcanvas.h"
#include "qgsmapcanvasmap.h"
//#include "qgscursors.h"
#include "qgsmaptopixel.h"
#include "qgsrubberband.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsmaptool.h"
#include "qgspoint.h"
#include "qgsapplication.h"
#include <qgsdistancearea.h>

#if defined(WIN32) || defined(WIN64)
#  include <windows.h>
#  define strdup _strdup
#endif

#define DRAW_CACHES false

//
// caches
// 0: drawing cache
// 5, 4, 3, 2, 1: stapled caches
// 6 contains the background picture
//
//-----------------------------------------------------------------------------
//
//MapProperty * MapProperty::obj;  // Initialize static member of class MapProperty (Singleton)
//
//
MyCanvas::MyCanvas(QgisInterface * QGisIface) :
    QgsMapTool(QGisIface->mapCanvas()),
    QgsMapCanvasItem(QGisIface->mapCanvas()),
    printing(false)
    {
    QgsMapTool::setCursor(QgsApplication::getThemeCursor(QgsApplication::Cursor::CrossHair));
    m_property = MapProperty::getInstance();

    mQGisIface = QGisIface;
    mMapCanvas = QGisIface->mapCanvas();
    mMapCanvasItem = QGisIface->mapCanvas();
    drawing = true;
    _ugrid_file = nullptr;
    _variable = nullptr;
    m_layer = 0;
    _current_step = 0;
    m_ramph = new QColorRampEditor();
    m_vscale_determined = false;
    m_vec_length = 0.0;

    qgis_painter = NULL;
    mCache_painter = new QPainter();

    //buffer0 = new QImage(IMAGE_WIDTH, IMAGE_HEIGHT, QImage::Format_ARGB32_Premultiplied);
    //buffer0->fill(Qt::transparent);

    for (int i=0; i<NR_CACHES; i++)
    {
        cacheArray[i] = NULL;
        newCache(i);
    }
    //
    // Render events
    // This calls the renderer everytime the canvas has drawn itself
    //
    connect(mMapCanvas, SIGNAL(renderComplete(QPainter *)), this, SLOT(renderCompletePlugin(QPainter *)));
    //
    // Key events
    //
    connect(mMapCanvas, SIGNAL(keyPressed(QKeyEvent *)), this, SLOT(MyKeyPressEvent(QKeyEvent *)));
    connect(mMapCanvas, SIGNAL(keyReleased(QKeyEvent *)), this, SLOT(MyKeyReleasedEvent(QKeyEvent *)));
    //
    // Mouse events
    //
    //connect(this, SIGNAL(MouseDoubleClickEvent(QMouseEvent *)), this, SLOT(MyMouseDoubleClickEvent(QMouseEvent *)));
    //connect(this, SIGNAL(MouseMoveEvent(QMouseEvent *)), this, SLOT(MyMouseMoveEvent(QMouseEvent *)));
    //connect(this, SIGNAL(MousePressEvent(QMouseEvent *)), this, SLOT(MyMousePressEvent(QMouseEvent *)));
    connect(this, SIGNAL(MouseReleaseEvent(QMouseEvent *)), this, SLOT(MyMouseReleaseEvent(QMouseEvent *)));
    connect(this, SIGNAL(WheelEvent(QWheelEvent *)), this, SLOT(MyWheelEvent(QWheelEvent *)));
    //connect(this, &QgsMapTool::wheelEvent, this, &MyCanvas::MyWheelEvent);

    //QObject::connect(ramph, SIGNAL(rampChanged()), ramph, SLOT(msg()));
    connect(m_ramph, SIGNAL(rampChanged()), this, SLOT(draw_all()));
    QObject::connect(m_ramph, &QColorRampEditor::rampChanged, this, &MyCanvas::draw_all);
    QObject::connect(m_ramph, &QColorRampEditor::rampChanged, m_ramph, &QColorRampEditor::msg);

    if (DRAW_CACHES) {
        InitDrawEachCaches(); // debug utility
    }
    listener = NULL;
}
//
//-----------------------------------------------------------------------------
//
MyCanvas::~MyCanvas()
{
    for (int j = 0; j < NR_CACHES; j++)
    {
        if (cacheArray[j] != NULL)
        {
            delete cacheArray[j];
        }
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_all()
{
    //QMessageBox::information(0, "Message", "MyCanvas::draw_all");
    //draw_dot_at_edge();
    draw_data_at_face();
    draw_dot_at_face();
    draw_dot_at_node();
    draw_data_along_edge();
    draw_line_at_edge();
    draw_vector_at_face();
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_dot_at_face()
{
    return;
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "face")
    {
        string var_name = _variable->var_name;
        struct _mesh2d * mesh2d = _ugrid_file->get_mesh2d();
        std_data_at_face = _ugrid_file->get_variable_values(var_name);

        z_value = std_data_at_face[_current_step];
        double missing_value = _variable->fill_value;
        rgb_color.resize(mesh2d->face[0]->x.size());
        determine_min_max(z_value, &m_z_min, &m_z_max, rgb_color, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();

        this->startDrawing(0);
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        this->setPointSize(13);
        this->drawMultiDot(mesh2d->face[0]->x, mesh2d->face[0]->y, rgb_color);
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_data_at_face()
{
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "face")
    {
        bool in_view = false;
        string var_name = _variable->var_name;
        struct _mesh2d * mesh2d = _ugrid_file->get_mesh2d();
        if (_variable->dims.size() == 2) // 2D: time, nodes
        {
            std_data_at_face = _ugrid_file->get_variable_values(var_name);
            z_value = std_data_at_face[_current_step];
        }
        else if (_variable->dims.size() == 3) // 3D: time, layer, nodes
        {
            vector<vector<vector <double *>>> std_data_at_face_3d = _ugrid_file->get_variable_3d_values(var_name);
            z_value = std_data_at_face_3d[_current_step][m_layer-1];
        }
        else
        {
            QMessageBox::information(0, tr("MyCanvas::draw_data_at_face()"), QString("Program error on variable: \"%1\"\nUnsupported number of dimensions (i.e. > 3).").arg(var_name.c_str()));
        }
        double missing_value = _variable->fill_value;
        rgb_color.resize(mesh2d->face_nodes.size());
        determine_min_max(z_value, &m_z_min, &m_z_max, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();

        this->startDrawing(0);
        mCache_painter->setPen(Qt::NoPen);  // The bounding line of the polygon is not drawn
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        vector<double> vertex_x(mesh2d->face_nodes[0].size());
        vector<double> vertex_y(mesh2d->face_nodes[0].size());
        for (int i = 0; i < mesh2d->face_nodes.size(); i++)
        {
            vertex_x.clear();
            vertex_y.clear();
            for (int j = 0; j < mesh2d->face_nodes[i].size(); j++)
            {
                int p1 = mesh2d->face_nodes[i][j];
                if ( p1 > -1)
                {
                    vertex_x.push_back(mesh2d->node[0]->x[p1]);
                    vertex_y.push_back(mesh2d->node[0]->y[p1]);
                    if (mesh2d->node[0]->x[p1] > getMinVisibleX() && mesh2d->node[0]->x[p1] < getMaxVisibleX() &&
                        mesh2d->node[0]->y[p1] > getMinVisibleY() && mesh2d->node[0]->y[p1] < getMaxVisibleY())
                    {
                        in_view = true; // point in visible area, do not skip thi polygon
                    }
                }
            }
            if (in_view && *z_value[i] != missing_value)
            {
                setFillColor(m_ramph->getRgbFromValue(*z_value[i]));
                this->drawPolygon(vertex_x, vertex_y);
            }
        }
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_vector_at_face()
{
    if (_ugrid_file != nullptr)
    {
        vector<double> coor_x(5);
        vector<double> coor_y(5);
        vector<double> coor_ax(2);
        vector<double> coor_ay(2);
        vector<double> coor_bx(2);
        vector<double> coor_by(2);
        vector<double> dx(5);
        vector<double> dy(5);
        struct _mesh2d * mesh2d = _ugrid_file->get_mesh2d();
        int dimens;
        double vscale;
        double missing_value;
        double b_len;

        if (m_coordinate_type.size() == 0) { return; }

        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        // get average cell size (ie sqrt(area))    
        struct _mesh_variable * vars = _ugrid_file->get_variables();
        if (!m_vscale_determined)
        {
            struct _variable * cell_area = _ugrid_file->get_var_by_std_name(vars, "cell_area");
            m_vec_length = statistics_averaged_length_of_cell(cell_area);

            if (m_coordinate_type[0] == "Spherical")
            {
                // TODO scale to degrees
                double radius_earth = 6371008.771;
                m_vec_length = m_vec_length / (2.0 * M_PI * radius_earth / 360.0);  // 1 degree on the great circle
            }
            m_vscale_determined = true;
        }
        double fac = m_property->get_vector_scaling();
        vscale = fac * m_vec_length;

        double vlen;
        double beta;
        dx[0] = 0.0;  // not used
        dx[1] = 1.0;
        dx[2] = 0.8;
        dx[3] = 0.8;
        dx[4] = 0.0;  // not used

        dy[0] = 0.0;  // not used
        dy[1] = 0.0;
        dy[2] = -0.1;
        dy[3] = 0.1;
        dy[4] = 0.0;  // not used
 
        this->setLineColor(1);
        this->setFillColor(1);
        this->setPointSize(7);
        this->setLineWidth(3);

        if (m_coordinate_type[0] == "Cartesian" || m_coordinate_type[0] == "Spherical")
        {
            // 
            for (int i = 0; i < vars->nr_vars; i++)
            {
                if (vars->variable[i]->var_name == m_coordinate_type[1].toStdString())
                {
                    dimens = vars->variable[i]->dims.size();
                    missing_value = vars->variable[i]->fill_value;
                }
            }
            if (dimens == 2) // 2D: time, nodes
            {
                vector<vector <double *>> std_u_vec_at_face = _ugrid_file->get_variable_values(m_coordinate_type[1].toStdString());
                u_value = std_u_vec_at_face[_current_step];
                vector<vector <double *>> std_v_vec_at_face = _ugrid_file->get_variable_values(m_coordinate_type[2].toStdString());
                v_value = std_v_vec_at_face[_current_step];
            }
            else if (dimens == 3) // 3D: time, layer, nodes
            {
                vector<vector<vector <double *>>> std_u_vec_at_face_3d = _ugrid_file->get_variable_3d_values(m_coordinate_type[1].toStdString());
                u_value = std_u_vec_at_face_3d[_current_step][m_layer - 1];
                vector<vector<vector <double *>>> std_v_vec_at_face_3d = _ugrid_file->get_variable_3d_values(m_coordinate_type[2].toStdString());
                v_value = std_v_vec_at_face_3d[_current_step][m_layer - 1];
            }
            else
            {
                QMessageBox::information(0, "MapTimeManagerWindow::draw_time_dependent_vector", QString("Layer velocities not yet implemented"));
            }
            this->startDrawing(0);
            //rgb_color.resize(u_value.size());
            //for (int i = 0; i < u_value.size(); i++)
            //{
            //    rgb_color[i] = qRgba(1, 0, 0, 255);
            //}
            this->setPointSize(3); 
            this->setFillColor(qRgba(0, 0, 255, 255));  

            for (int i = 0; i < u_value.size(); i++)
            {
                if (*u_value[i] == missing_value) { continue; }  // *u_value[i] = 0.0;
                if (*v_value[i] == missing_value) { continue; }  // *v_value[i] = 0.0;

                coor_x.clear();
                coor_y.clear();
                coor_ax.clear();
                coor_ay.clear();
                coor_bx.clear();
                coor_by.clear();

                coor_x.push_back(mesh2d->face[0]->x[i]);
                coor_y.push_back(mesh2d->face[0]->y[i]);

                if (coor_x[0] < getMinVisibleX() || coor_x[0] > getMaxVisibleX() ||
                    coor_y[0] < getMinVisibleY() || coor_y[0] > getMaxVisibleY())  
                {
                    continue; // root of vector not in visible area, skip this vector
                }

                vlen = sqrt(*u_value[i] * *u_value[i] + *v_value[i] * *v_value[i]);  // The "length" of the vector
                beta = atan2(*v_value[i], *u_value[i]);
                if (vscale * vlen < 0.00001)
                {
                    vlen = 0.0;
                }
                else
                {
                    vlen = max(vscale * vlen, 0.0001);
                }
                for (int k = 1; k < 4; k++)
                {
                    coor_x.push_back(coor_x[0] + vlen * (cos(beta) * dx[k] - sin(beta) * dy[k]));
                    coor_y.push_back(coor_y[0] + vlen * (sin(beta) * dx[k] + cos(beta) * dy[k]));
                }

                coor_x.push_back(coor_x[1]);
                coor_y.push_back(coor_y[1]);

                if (m_coordinate_type[0] == "Spherical")
                {
                    double x_tmp;
                    double y_tmp;
                    double fac = std::cos(M_PI / 180.0 * coor_y[0]);

                    coor_x[1] = coor_x[1];
                    coor_y[1] = coor_y[0] + fac * (coor_y[1] - coor_y[0]);

                    coor_bx[0] = coor_x[0] + 0.8 * (coor_x[1] - coor_x[0]);
                    coor_by[0] = coor_y[0] + 0.8 * (coor_y[1] - coor_y[0]);

                    double eps = 1e-8;
                    if (abs(coor_y[1] - coor_y[0]) > eps)
                    {
                        b_len = 0.1 * vlen;
                        coor_bx[1] = b_len + coor_bx[0];
                        coor_by[1] = -(coor_x[1] - coor_x[0]) * b_len / (coor_y[1] - coor_y[0]) + coor_by[0];

                        //double vlen_b = sqrt((coor_bx[1] - coor_bx[0]) * (coor_bx[1] - coor_bx[0]) + (coor_by[1] - coor_by[0]) * (coor_by[1] - coor_by[0]));
                        double vlen_b = sqrt(b_len * b_len + (coor_by[1] - coor_by[0]) * (coor_by[1] - coor_by[0]));
                        
                        //double b_len = 0.1 * vlen / vlen_b;
                        x_tmp = 0.1 * vlen * (coor_bx[1] - coor_bx[0]) / (vlen_b);
                        y_tmp = 0.1 * vlen * (coor_by[1] - coor_by[0]) / (vlen_b);

                        coor_x[2] = coor_bx[0] - x_tmp;
                        coor_y[2] = coor_by[0] - y_tmp;
                        coor_x[3] = coor_bx[0] + x_tmp;
                        coor_y[3] = coor_by[0] + y_tmp;
                    }
                    else
                    {
                        // y_1 - y_0 === 0
                        b_len = 0.1 * vlen;
                        coor_bx[1] = b_len + coor_bx[0];
                        coor_by[1] = b_len + coor_by[0];
                        //vlen = sqrt((coor_x[1] - coor_x[0]) * (coor_x[1] - coor_x[0]) + (coor_y[1] - coor_y[0]) * (coor_y[1] - coor_y[0]));  // The "length" of the vector
                        vlen = coor_x[1] - coor_x[0];  // The "length" of the vector
                        coor_x[2] = coor_x[0] + 0.8 * vlen;
                        coor_y[2] = coor_y[0] + 0.1 * vlen;
                        coor_x[3] = coor_x[0] + 0.8 * vlen;
                        coor_y[3] = coor_y[0] - 0.1 * vlen;


                    }
                    coor_x[4] = coor_x[1];
                    coor_y[4] = coor_y[1];
                }
                this->drawPolyline(coor_x, coor_y);
            }
            this->finishDrawing();

        }
        //else if (m_coordinate_type[0] == "Spherical")
        {
            // east_sea_water_velocity, north_sea_water_velocity
        }
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_dot_at_node()
{
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "node")
    {
        string var_name = _variable->var_name;
        struct _mesh1d * mesh1d = _ugrid_file->get_mesh1d();
        std_data_at_node = _ugrid_file->get_variable_values(var_name);

        z_value = std_data_at_node[_current_step];
        double missing_value = _variable->fill_value;
        rgb_color.resize(mesh1d->node[0]->x.size());
        determine_min_max(z_value, &m_z_min, &m_z_max, rgb_color, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();

        this->startDrawing(0);
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        this->setPointSize(13);
        this->drawMultiDot(mesh1d->node[0]->x, mesh1d->node[0]->y, rgb_color);
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_dot_at_edge()
{
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "edge")
    {
        double x1, y1, x2, y2;
        vector<double> edge_x;
        vector<double> edge_y;
        string var_name = _variable->var_name;
        struct _mesh1d * mesh1d = _ugrid_file->get_mesh1d();
        struct _edge * edges = mesh1d->edge[0];
        if (_variable->dims.size() == 2) // 2D: time, nodes
        {
            std_dot_at_edge = _ugrid_file->get_variable_values(var_name);
            z_value = std_dot_at_edge[_current_step];
        }
        else if (_variable->dims.size() == 3) // 3D: time, layer, nodes
        {
            vector<vector<vector <double *>>> std_dot_at_edge_3d = _ugrid_file->get_variable_3d_values(var_name);
            z_value = std_dot_at_edge_3d[_current_step][m_layer - 1];
        }
        else
        {
            QMessageBox::information(0, tr("MyCanvas::draw_dot_at_edge()"), QString("Program error on variable: \"%1\"\nUnsupported number of dimensions (i.e. > 3).").arg(var_name.c_str()));
        }

        double missing_value = _variable->fill_value;
        rgb_color.resize(edges->count);
        determine_min_max(z_value, &m_z_min, &m_z_max, rgb_color, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();

        for (int j = 0; j < edges->count; j++)
        {
            int p1 = edges->edge_nodes[j][0];
            int p2 = edges->edge_nodes[j][1];
            x1 = mesh1d->node[0]->x[p1];
            y1 = mesh1d->node[0]->y[p1];
            x2 = mesh1d->node[0]->x[p2];
            y2 = mesh1d->node[0]->y[p2];

            edge_x.push_back(0.5*(x1 + x2));
            edge_y.push_back(0.5*(y1 + y2));
        }

        this->startDrawing(0);
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        this->setPointSize(13);
        this->drawMultiDot(edge_x, edge_y, rgb_color);
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }
}
//-----------------------------------------------------------------------------
void MyCanvas::draw_line_at_edge()
{
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "edge")
    {
        vector<double> edge_x(2);
        vector<double> edge_y(2);
        string var_name = _variable->var_name;
        if (_variable->dims.size() == 2) // 2D: time, nodes
        {
            std_dot_at_edge = _ugrid_file->get_variable_values(var_name);
            z_value = std_dot_at_edge[_current_step];
        }
        else if (_variable->dims.size() == 3) // 3D: time, layer, nodes
        {
            vector<vector<vector <double *>>> std_dot_at_edge_3d = _ugrid_file->get_variable_3d_values(var_name);
            z_value = std_dot_at_edge_3d[_current_step][m_layer - 1];
        }
        else
        {
            QMessageBox::information(0, tr("MyCanvas::draw_dot_at_edge()"), QString("Program error on variable: \"%1\"\nUnsupported number of dimensions (i.e. > 3).").arg(var_name.c_str()));
        }
        if (z_value.size() == 0)
        {
            return;
        }
        double missing_value = _variable->fill_value;
        determine_min_max(z_value, &m_z_min, &m_z_max, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();

        struct _edge * edges = nullptr;
        struct _mesh1d * mesh1d = nullptr;
        struct _mesh1d_string ** m1d = _ugrid_file->get_mesh1d_string();
        if (m1d != nullptr && _variable->mesh == m1d[0]->var_name)
        {
            mesh1d = _ugrid_file->get_mesh1d();
            edges = mesh1d->edge[0];
        }

        struct _mesh2d * mesh2d = nullptr;
        struct _mesh2d_string ** m2d = _ugrid_file->get_mesh2d_string();
        if (m2d != nullptr && _variable->mesh == m2d[0]->var_name)
        {
            mesh2d = _ugrid_file->get_mesh2d();
            edges = mesh2d->edge[0];
        }

        struct _mesh_contact * mesh1d2d = nullptr;
        struct _mesh_contact_string ** m1d2d = _ugrid_file->get_mesh_contact_string();
        if(m1d2d != nullptr && _variable->mesh == m1d2d[0]->mesh_contact)
        {
            mesh1d2d = _ugrid_file->get_mesh_contact();
            edges = mesh1d2d->edge[0];
        }

        rgb_color.resize(edges->count);
        this->startDrawing(0);
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        for (int j = 0; j < edges->count; j++)
        {
            int p1 = edges->edge_nodes[j][0];
            int p2 = edges->edge_nodes[j][1];
            if (mesh1d != nullptr)
            {
                edge_x[0] = mesh1d->node[0]->x[p1];
                edge_y[0] = mesh1d->node[0]->y[p1];
                edge_x[1] = mesh1d->node[0]->x[p2];
                edge_y[1] = mesh1d->node[0]->y[p2];
            }
            else if (mesh2d != nullptr)
            {
                edge_x[0] = mesh2d->node[0]->x[p1];
                edge_y[0] = mesh2d->node[0]->y[p1];
                edge_x[1] = mesh2d->node[0]->x[p2];
                edge_y[1] = mesh2d->node[0]->y[p2];
            }
            else if (mesh1d2d != nullptr)
            {
                edge_x[0] = mesh1d2d->node[0]->x[p1];
                edge_y[0] = mesh1d2d->node[0]->y[p1];
                edge_x[1] = mesh1d2d->node[0]->x[p2];
                edge_y[1] = mesh1d2d->node[0]->y[p2];
            }
            this->setLineColor(m_ramph->getRgbFromValue(*z_value[j]));
            // 10 klasses: 2, 3, 4, 5, 6, 7, 8, 9 ,10, 11
            //double alpha = (*z_value[j] - m_z_min) / (m_z_max - m_z_min);
            //double width = (1. - alpha) * 1. + alpha * 11.;
            this->setLineWidth(5);
            
            this->drawPolyline(edge_x, edge_y);
        }
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }
}

//-----------------------------------------------------------------------------
void MyCanvas::draw_data_along_edge()
{
    if (_variable != nullptr && _ugrid_file != nullptr && _variable->location == "node")
    {
        string var_name = _variable->var_name;
        struct _mesh1d * mesh1d = _ugrid_file->get_mesh1d();
        std_data_at_node = _ugrid_file->get_variable_values(var_name);

        dims = _variable->dims;

        struct _edge * edges = mesh1d->edge[0];
        this->startDrawing(0);
        double opacity = mCache_painter->opacity();
        mCache_painter->setOpacity(m_property->get_opacity());
        this->setPointSize(13);
        vector<double> edge_x(2);
        vector<double> edge_y(2);
        vector<int> edge_color(2);

        z_value = std_data_at_node[_current_step];
        double missing_value = _variable->fill_value;
        determine_min_max(z_value, &m_z_min, &m_z_max, missing_value);
        m_ramph->setMinMax(m_z_min, m_z_max);
        m_ramph->update();
        if (true)  // boolean to draw gradient along line?
        {
            for (int j = 0; j < edges->count; j++)
            {
                int p1 = edges->edge_nodes[j][0];
                int p2 = edges->edge_nodes[j][1];
                edge_x[0] = mesh1d->node[0]->x[p1];
                edge_y[0] = mesh1d->node[0]->y[p1];
                edge_x[1] = mesh1d->node[0]->x[p2];
                edge_y[1] = mesh1d->node[0]->y[p2];

                edge_color[0] = m_ramph->getRgbFromValue(*z_value[p1]);
                edge_color[1] = m_ramph->getRgbFromValue(*z_value[p2]);

                this->drawLineGradient(edge_x, edge_y, edge_color);
            }
        }
        if (false)  // boolean to draw multidot?
        {
            rgb_color.resize(z_value.size());
            for (int i = 0; i < z_value.size(); i++)
            {
                rgb_color[i] = m_ramph->getRgbFromValue(*z_value[i]);
            }
            this->drawMultiDot(mesh1d->node[0]->x, mesh1d->node[0]->y, rgb_color);
        }
        mCache_painter->setOpacity(opacity);
        this->finishDrawing();
    }

}
void MyCanvas::setColorRamp(QColorRampEditor * ramph)
{
    m_ramph = ramph;
}

//-----------------------------------------------------------------------------
void MyCanvas::reset_min_max()
{
    m_z_min = std::numeric_limits<double>::infinity();
    m_z_max = -std::numeric_limits<double>::infinity();
}
//-----------------------------------------------------------------------------
void MyCanvas::set_coordinate_type(QStringList coord_type)
{
    m_coordinate_type = coord_type;
}
//-----------------------------------------------------------------------------
void MyCanvas::set_current_step(int current_step)
{
    _current_step = current_step;
}
//-----------------------------------------------------------------------------
void MyCanvas::set_variable(struct _variable * variable)
{
    _variable = variable;
    this->reset_min_max();
}
//-----------------------------------------------------------------------------
void MyCanvas::set_variables(struct _mesh_variable * variables)
{
    m_variables = variables;
}
//-----------------------------------------------------------------------------
void MyCanvas::set_layer(int i_layer)
{
    this->m_layer = i_layer;
}

//-----------------------------------------------------------------------------
void MyCanvas::setUgridFile(UGRID * ugrid_file)
{
    _ugrid_file = ugrid_file;
}
//-----------------------------------------------------------------------------
void MyCanvas::determine_min_max(vector<double *> z, double * z_min, double * z_max, vector<int> &rgb_color, double missing_value)
{
    if (m_property->get_dynamic_legend())
    {
        for (int i = 0; i < z.size(); i++)
        {
            if (*z[i] != missing_value)
            {
                *z_min = min(*z_min, *z[i]);
                *z_max = max(*z_max, *z[i]);
                rgb_color[i] = m_ramph->getRgbFromValue(*z[i]);
            }
            else
            {
                rgb_color[i] = qRgba(255, 0, 0, 255);
            }
        }
    }
    else
    {
        *z_min = m_property->get_minimum();
        *z_max = m_property->get_maximum();
    }
}
void MyCanvas::determine_min_max(vector<double *> z, double * z_min, double * z_max, double missing_value)
{
    if (m_property->get_dynamic_legend())
    {
        *z_min = INFINITY;
        *z_max = -INFINITY;
        for (int i = 0; i < z.size(); i++)
        {
            if (*z[i] != missing_value)
            {
                *z_min = min(*z_min, *z[i]);
                *z_max = max(*z_max, *z[i]);
            }
        }
        m_property->set_minimum(*z_min);
        m_property->set_maximum(*z_max);
    }
    else
    {
        *z_min = m_property->get_minimum();
        *z_max = m_property->get_maximum();
    }
}

double MyCanvas::statistics_averaged_length_of_cell(struct _variable * var)
{
    double avg_cell_area;
    vector <double *> area = var->z_value[0];

    avg_cell_area = 0.0;
    for (int i = 0; i < area.size(); i++)
    {
        avg_cell_area += *area[i];
    }
    avg_cell_area /= area.size();
    return std::sqrt(avg_cell_area);
}
//-----------------------------------------------------------------------------
void MyCanvas::empty_caches()
{
    for (int j = 0; j<NR_CACHES; j++)
    {
        if (cacheArray[j] != NULL)
        {
            cacheArray[j]->fill(Qt::transparent);
        }
    }
    mMapCanvas->update();  // todo, niet uitvoeren na openen van twee map-files
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::paint( QPainter * p )
{
    //p->drawImage(0, 0, *cacheArray[NR_CACHES - 1]);
    for (int i = NR_CACHES - 1; i >= 0; i--) {
        p->drawImage(0, 0, *cacheArray[i]);
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::startDrawing(int cache_i)
{
    bool clear_cache;

    if (printing) return;
    clear_cache = true;

    cache_i = max(0, min(cache_i, NR_CACHES - 1));

    if (drawing)
    {
        if (mCache_painter->isActive()) {
            mCache_painter->end(); // end painting on cacheArray[i]
        }
    }
    if (clear_cache) {
        cacheArray[cache_i]->fill(Qt::transparent);
    }
    mCache_painter->begin(cacheArray[cache_i]);
    drawCache = cache_i;
    drawing = true;

    if (DRAW_CACHES) {
        DrawEachCaches(); // debug utility
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::finishDrawing()
{
    // QMessageBox::warning(0, "Message", QString(tr("MyCanvas::finishDrawing.")));
    if (mCache_painter->isActive()) {
        mCache_painter->end();
    }
    mMapCanvas->update();  // needed for initial drawing
    drawing = false;

    if (DRAW_CACHES) {
        DrawEachCaches(); // debug utility
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::renderCompletePlugin(QPainter * Painter)
{
    //QMessageBox::warning(0, "Message", QString(tr("MyCanvas::renderCompletePlugin") ));
    renderPlugin( Painter );
}
void MyCanvas::renderPlugin( QPainter * Painter )
{
    // OK JanM QMessageBox::warning(0, "Message", QString(tr("MyCanvas::renderPlugin") ));
    // need width/height of paint device
    int myWidth = Painter->device()->width();  //pixels
    int myHeight = Painter->device()->height(); //pixels
    int width  = 500; //pixels
    int height = 250; //pixels

    this->qgis_painter = Painter;

    mMapCanvas->setMinimumWidth(width);
    mMapCanvas->setMinimumHeight(height);
    scale = mMapCanvas->scale();

    QRect frame = mMapCanvas->frameRect();
    frame_width = frame.width(); //total frame width in pixels
    frame_height = frame.height();  //total height width in pixels

    QgsRectangle extent = mMapCanvas->extent();
    window_width = extent.width();   //total frame width in world coordinates
    window_height = extent.height(); //total height width in world coordinates
    min_X = extent.xMinimum();
    min_Y = extent.yMinimum();

    dx = window_width/frame_width;
    dy = window_height/frame_height;

    this->qgis_painter->setViewport(qx(min_X), qy(min_Y), qx(window_width), qy(window_height));    

    if (listener != NULL)
    {
        listener->onAfterRedraw(false); // TODO: In onafterredraw zit ook een teken event
    }
    //draw_dot_at_edge();
    draw_data_at_face();
    draw_dot_at_face();
    draw_dot_at_node();
    draw_data_along_edge();
    draw_line_at_edge();
    draw_vector_at_face();
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::canvasDoubleClickEvent(QgsMapMouseEvent * me )
{
    emit MouseDoubleClickEvent( me );
}
void MyCanvas::canvasMoveEvent(QgsMapMouseEvent * me )
{
    //QMessageBox::warning(0, "Message", QString(tr("MyCanvas::canvasMoveEvent: %1").arg(me->button())));
    emit MouseMoveEvent( me );
}
void MyCanvas::canvasPressEvent(QgsMapMouseEvent * me )
{
    emit MousePressEvent( me );
}
void MyCanvas::canvasReleaseEvent(QgsMapMouseEvent * me )
{
    if (me->button() == Qt::RightButton)
    {
        QMessageBox::warning(0, "Message", QString(tr("MyCanvas::canvasReleaseEvent: %1").arg(me->button())));
        // get selected feature from selected layer
        // then view contex menu
        // launch the choose action
        QgsLayerTree * treeRoot = QgsProject::instance()->layerTreeRoot();  // root is invisible
        QList <QgsLayerTreeGroup *> groups = treeRoot->findGroups();
        QgsMapLayer * active_layer = NULL;
            //active_layer = mQGisIface->activeLayer();
            if (active_layer != nullptr)
            {
                QString layer_id = active_layer->id();
                int a = -1;
            }
    }
    emit MyMouseReleaseEvent( me );
}
void MyCanvas::wheelEvent( QWheelEvent * we )
{
    //QMessageBox::warning(0, "Message", QString(tr("MyCanvas::wheelEvent")));
    this->empty_caches();
    mMapCanvas->update();
    emit WheelEvent( we );
}
//
//-----------------------------------------------------------------------------
//
// Create a new draw cache in the frontend and it gets the given index.
bool MyCanvas::newCache(int cacheNr)
{
    if ( cacheNr < 0  ||  cacheNr > NR_CACHES-1 )
    {
        return false;
    }

    if ( cacheArray[cacheNr] == NULL )
    {
        cacheArray[cacheNr] = new QImage(IMAGE_WIDTH, IMAGE_HEIGHT, QImage::Format_ARGB32_Premultiplied);
    }

    return true;
}
//
//-----------------------------------------------------------------------------
//
// Get the cache index of default available draw cache of the map frontend.
int MyCanvas::getBackgroundCache()
{
    return NR_CACHES-1; // Cache in which background should be copyied
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::copyCache(int sourceCacheIndex, int destCacheIndex)
{
	// No need for this function, still avalaible because of interface with plugins
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::drawDot(double x, double y)
{
    // Don't draw an edge
    QPen current_pen = mCache_painter->pen();
    mCache_painter->setPen( Qt::NoPen );
    QPoint center = QPoint(qx(x), qy(y));
    mCache_painter->drawEllipse( center, int(radius), int(radius) );
    mCache_painter->setPen( current_pen );
}

void MyCanvas::drawMultiDot(vector<double> xs , vector<double> ys , vector<int> rgb)
{
    int    i, j;
    size_t k;
    int    index;
    QPen   current_pen;
    double xMin, xMax, yMin, yMax;
    int    iMin, iMax, jMin, jMax;
    int    sizeI, sizeJ;
    double sx, sy;

    size_t nrPoints = xs.size();

    if (radius==0) {return;}
    if (radius < 4.0) { // i.e. 1, 2 and 3
        drawMultiPoint(xs , ys , rgb);
        return;
    }

    // NOTE that in this function is a check if the coordinates are outside the screen/display
    //
    // These are world coordinates
    //
    xMin = getMinVisibleX();
    xMax = getMaxVisibleX();
    yMin = getMinVisibleY();
    yMax = getMaxVisibleY();

    iMin = qx( xMin );
    iMax = qx( xMax );
    // function qy() mirrors the min and max!!!
    jMax = qy( yMin );
    jMin = qy( yMax );

    sizeI = iMax - iMin + 1;
    sizeJ = jMax - jMin + 1;

    int *pixelArray = new int [sizeI*sizeJ];

    // Initial value 0
    //
    memset(pixelArray, 0, (sizeI*sizeJ) * sizeof(int));

    // Don't draw an edge
    current_pen = mCache_painter->pen();
    mCache_painter->setPen( Qt::NoPen );


    // Loop for all point
    //
    sx = radius/scale; // scale x
    sy = radius/scale; // scale y

    for ( k = 0; k != nrPoints; k++ )
    {
        if (xs[k] < getMinVisibleX() || xs[k] > getMaxVisibleX() ||
            ys[k] < getMinVisibleY() || ys[k] > getMaxVisibleY())
        {
            continue; // point not in visible area, skip this vector
        }
        // Convert from world to pixel coordinates.
        // Functions qx() and qy() take into account that (0,0) is upper left corner
        //
        i = qx( xs[k]-sx );
        j = qy( ys[k]+sy );      //Y-flip

        if ( i >= 0 && i < sizeI && j >= 0 && j < sizeJ)
        {
            index = i + j*sizeI;
            if ( pixelArray[index] == 0 )
            {
                // Pixel not drawn yet.
                //
                mCache_painter->setBrush( QColor( rgb[k] ) );
                mCache_painter->drawEllipse( QPoint(i, j), int(radius), int(radius));
                pixelArray[index] = 1;
            }
        }
    }

    mCache_painter->setPen( current_pen );

    delete[] pixelArray;
}
//
//-----------------------------------------------------------------------------
//
// Draw a single point with currently set colour by routine fillColor()
void MyCanvas::drawPoint(double x, double y)
{
    QPen current_pen = mCache_painter->pen();
    mCache_painter->setPen(Qt::NoPen);
    QPoint center = QPoint(qx(x), qy(y));
    mCache_painter->drawEllipse(center, int(0.5*radius), int(0.5*radius));
    mCache_painter->setPen(current_pen);
}
//
//-----------------------------------------------------------------------------
//
// Draw an array of points according the given array of colours
void MyCanvas::drawMultiPoint(vector<double> xs, vector<double> ys, vector<int> rgb)
{
    int    i, j;
    size_t k;
    double xMin, xMax, yMin, yMax;
    int    iMin, iMax, jMin, jMax;
    int    sizeI, sizeJ;
    unsigned int * buffer_;
    unsigned int transparent;
    unsigned int colour;

    size_t nrPoints = xs.size();

    // NOTE that in this function is a check if the coordinates are outside the screen/display
    //
    // These are world coordinates
    //
    xMin = getMinVisibleX();
    xMax = getMaxVisibleX();
    yMin = getMinVisibleY();
    yMax = getMaxVisibleY();

    iMin = qx(xMin);
    iMax = qx(xMax);
    // function qy() mirrors the min and max!!!
    jMax = qy(yMin);
    jMin = qy(yMax);

    sizeI = iMax - iMin + 1;
    sizeJ = jMax - jMin + 1;

    buffer_ = new unsigned int[sizeI*sizeJ];
    memset(buffer_, 0, (sizeI*sizeJ) * sizeof(int));

    transparent = QColor(Qt::transparent).rgba();

    for (k = 0; k < sizeI*sizeJ; k++)
    {
        buffer_[k] = transparent;
    }

    // Loop for all points
    //
    if (radius < 2.0) { // radius == 1
        for (k = 0; k < nrPoints; k++)
        {
            i = qx(xs[k]);
            j = qy(ys[k]);
            if (i >= 0 + 1 && i < sizeI - 1 && j >= 0 + 1 && j < sizeJ - 1)
            {
                // Draw a 'plus' symbol on screen.
                colour = QColor(rgb[k]).rgba();
                buffer_[i - 1 + j   *sizeI] = colour;
                buffer_[i + j   *sizeI] = colour;
                buffer_[i + 1 + j   *sizeI] = colour;
                buffer_[i + (j - 1)*sizeI] = colour;
                buffer_[i + (j + 1)*sizeI] = colour;
            }
        }
    }
    else if (radius < 3.0) { // radius == 2
        for (k = 0; k < nrPoints; k++)
        {
            i = qx(xs[k]);
            j = qy(ys[k]);
            if (i >= 0 + 1 && i < sizeI - 1 && j >= 0 + 1 && j < sizeJ - 1)
            {
                // Draw a '2x2 square' symbol on screen.
                colour = QColor(rgb[k]).rgba();
                buffer_[i + j   *sizeI] = colour;
                buffer_[i + 1 + j   *sizeI] = colour;
                buffer_[i + (j + 1)*sizeI] = colour;
                buffer_[i + 1 + (j + 1)*sizeI] = colour;
            }
        }
    }
    else if (radius < 4.0) { // radius == 3
        for (k = 0; k < nrPoints; k++)
        {
            i = qx(xs[k]);
            j = qy(ys[k]);
            if (i >= 0 + 1 && i < sizeI - 1 && j >= 0 + 1 && j < sizeJ - 1)
            {
                // Draw a '3x3 square' symbol on screen.
                colour = QColor(rgb[k]).rgba();
                buffer_[i - 1 + (j - 1)*sizeI] = colour;
                buffer_[i + (j - 1)*sizeI] = colour;
                buffer_[i + 1 + (j - 1)*sizeI] = colour;
                buffer_[i - 1 + j   *sizeI] = colour;
                buffer_[i + j   *sizeI] = colour;
                buffer_[i + 1 + j   *sizeI] = colour;
                buffer_[i - 1 + (j + 1)*sizeI] = colour;
                buffer_[i + (j + 1)*sizeI] = colour;
                buffer_[i + 1 + (j + 1)*sizeI] = colour;
            }
        }
    }
    mCache_painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
    QImage* qimage = new QImage((unsigned char*)buffer_, sizeI, sizeJ, QImage::Format_ARGB32_Premultiplied);
    mCache_painter->drawImage(0, 0, *qimage);
    delete[] buffer_;
    delete qimage;
}
//-----------------------------------------------------------------------------
//
// Draw a polygon. The polygon gets a color according the routine setFillColor
void MyCanvas::drawPolygon(vector<double> xs, vector<double> ys)
{
	QPolygon polygon;
    for (int i=0; i<xs.size(); i++)
    {
        polygon << QPoint(qx(xs[i]), qy(ys[i]));
    }
    mCache_painter->drawPolygon(polygon);
}
//
//-----------------------------------------------------------------------------
//
// Draw a polyline. This line width and line colour has to be set before this call
void MyCanvas::drawPolyline(vector<double> xs, vector<double> ys)
{
    assert( xs.size() > 0 );
    QPolygon polyline;
    for (int i = 0; i < xs.size(); i++)
    {
        polyline << QPoint( qx(xs[i]),  qy(ys[i]));
    }
    //mCache_painter->setPen(QPen(7));
    mCache_painter->drawPolyline(polyline);
}
void MyCanvas::drawLineGradient(vector<double> xs, vector<double> ys, vector<int> rgb)
{
    assert(xs.size() == 2);
    QVector<QPair<QPoint, QColor>> point;
    point << qMakePair(QPoint(qx(xs[0]), qy(ys[0])), rgb[0]);
    point << qMakePair(QPoint(qx(xs[1]), qy(ys[1])), rgb[1]);

    QLinearGradient gradient;
    gradient.setColorAt(0, point[0].second);
    gradient.setColorAt(1, point[1].second);
    gradient.setStart(point[0].first);
    gradient.setFinalStop(point[1].first);
    mCache_painter->setPen(QPen(QBrush(gradient), 7));

    mCache_painter->drawLine(point[0].first, point[1].first);
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::drawRectangle(double x, double y, int width, int height)
{
    mCache_painter->drawRect( qx(x), qy(y), width, height);
}
//
//-----------------------------------------------------------------------------
//
// Draw the text. Textposition, font size and color has to bet set first by routines setFont.....
// and setTextAlignment()
void MyCanvas::drawText(double xleft, double ybottom, int width, int height, const char* text)
{
    QPen currentPen(mCache_painter->pen() );
    mCache_painter->setPen( textColour );

    mCache_painter->drawText( QPoint( qx(xleft), qy(ybottom) ), QString(text) );

    mCache_painter->setPen( currentPen );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setPointSize(int size)
{
    radius = double(size) /2.; // 2 is diameter/radius
    radius += 1.; // Empirical / correct for edge type "Qt::NoPen"
    if (size == 0) radius = 0.;
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setLineWidth(int width)
{
    QPen currentPen(mCache_painter->pen() );
    currentPen.setWidth( width );
    currentPen.setCapStyle(Qt::FlatCap);
    currentPen.setJoinStyle(Qt::RoundJoin);
    mCache_painter->setPen( currentPen );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setLineColor(int rgb)
{
    QPen currentPen(mCache_painter->pen() );
    currentPen.setColor( rgb );
    currentPen.setCapStyle(Qt::FlatCap);
    currentPen.setJoinStyle(Qt::RoundJoin);
    mCache_painter->setPen( currentPen );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFillColor(int rgb)
{
    if (rgb == 0) {
        mCache_painter->setBrush(Qt::NoBrush );
    } else {
		QBrush * qbr =  new QBrush( QColor( rgb ) );
        mCache_painter->setBrush(*qbr);
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontName(const char* name)
{
    assert(mCache_painter); // True only inside a paintEvent

    // The mCache_painter gets a new font.
    mCache_painter->setFont( QFont(name) );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontColor(int rgb)
{
    textColour.setRgb( QRgb( rgb ) );
    textColour.setRgb( QRgb( rgb ) );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontPointSize(int size)
{
    QFont currentFont(mCache_painter->font() );
    currentFont.setPointSize( size );
    mCache_painter->setFont( currentFont );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontItalic(bool value)
{
    QFont currentFont(mCache_painter->font() );
    currentFont.setItalic( value );
    mCache_painter->setFont( currentFont );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontBold(bool value)
{
    //=======om te testen met pixmap================
    //buffer0->load( QString("D:\\NL-Zuidwest.bmp") , "BMP" , 0 ) ;
    //buffer0->save( QString("D:\\QtPixmap.jpg") , "JPEG" ) ;

    //=====dit is de originele code
    //QFont newfont( mCache_painter->font() );
    //newfont.setBold( isBold );
    //mCache_painter->setFont( newfont );
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFontUnderline(bool value)
{
    QFont currentFont(mCache_painter->font() );
    currentFont.setUnderline( value );
    mCache_painter->setFont( currentFont );
}
//
//-----------------------------------------------------------------------------
//
bool MyCanvas::isFontAvailable(const char* name)
{
    return (bool) true; //TODO implementation
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getTextWidth(const char* name)
{
    int size = (mMapCanvas->fontMetrics()).width(name);
    return size;
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getTextHeight(const char* name, int maxWidth)
{
    int size = (mMapCanvas->fontMetrics()).height();
    return size;
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getPixelWidth(double x, double y)
{
   return dx; // height of one pixel in world co-ordinates
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getPixelHeight(double x, double y)
{
   return dy; // height of one pixel in world co-ordinates
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMinX()
{
   return min_X;
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMaxX()
{
   return max_X;
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMinY()
{
   return min_Y;
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMaxY()
{
   return max_Y;
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMinVisibleX()
{
    QgsRectangle extent = mMapCanvas->extent();
    return extent.xMinimum();
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMaxVisibleX()
{
    QgsRectangle extent = mMapCanvas->extent();
    return extent.xMaximum();
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMinVisibleY()
{
    QgsRectangle extent = mMapCanvas->extent();
    return extent.yMinimum();
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getMaxVisibleY()
{
    QgsRectangle extent = mMapCanvas->extent();
    return extent.yMaximum();
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getWidth()
{
    QRect frame = mMapCanvas->frameRect();
    return frame.width();
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getHeight()
{
    QRect frame = mMapCanvas->frameRect();
    return frame.height();
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getPixelXFromXY(double x, double y)
{
    return qx((double) x);
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getPixelYFromXY(double x, double y)
{
   return qy((double) y);
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getXFromPixel(int pixelX, int pixelY)
{
    return wx(pixelX);
}
//
//-----------------------------------------------------------------------------
//
double MyCanvas::getYFromPixel(int pixelX, int pixelY)
{
    return wy(pixelY);
}
//
//-----------------------------------------------------------------------------
//
int MyCanvas::getPixelColor(double x, double y)
{
    return cacheArray[drawCache]->pixel((int)wx(x), (int)wy(y));
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::setFullExtend(double minX, double maxX, double minY, double maxY)
{
    // Full exent is set by the MFE (ie QGIS, ArcGIS, ...)
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::zoomToExtend(double minX, double maxX, double minY, double maxY)
{
    // Zoom to exent is done by the MFE (ie QGIS, ArcGIS, ...)
}

//
//-----------------------------------------------------------------------------
// ********* Events a widget must handle or forward to plugin **********
//-----------------------------------------------------------------------------
//
//

void MyCanvas::MyWheelEvent ( QWheelEvent * we )
{
    //QMessageBox::warning( 0, "Message", QString(tr("MyCanvas::MyWheelEvent")));
    mMapCanvas->update();
}

//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyMouseDoubleClickEvent( QMouseEvent * me)
{
    QMessageBox::warning( 0, "Message", QString(tr("MyCanvas::mouseDoubleClickEvent: %1").arg(me->button())));
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyMouseMoveEvent      ( QMouseEvent * me)
{
    if (listener != NULL)
    {
        //QMessageBox::warning(0, "Message", QString(tr("MyCanvas::MyMouseMoveEvent: %1").arg(me->button())));
        listener->onMouseMove(wx(me->x()), wy(me->y()), (AbstractCanvasListener::ButtonState) me->button() );
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyMousePressEvent     ( QMouseEvent * me)
{
    if (listener != NULL)
    {
        listener->onMouseDown(wx(me->x()), wy(me->y()), (AbstractCanvasListener::ButtonState) me->button() );
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyMouseReleaseEvent   (QgsMapMouseEvent * me)
{
    double length; 
    // afstand  bepaling
    //  crs = self.iface.mapCanvas().mapRenderer().destinationCrs()
    //distance_calc = QgsDistanceArea()
    //    distance_calc.setSourceCrs(crs)
    //    distance_calc.setEllipsoid(crs.ellipsoidAcronym())
    //    distance_calc.setEllipsoidalMode(crs.geographicFlag())
    //    distance = distance_calc.measureLine([self._startPt, endPt]) / 1000
    QgsPointXY p1 = QgsMapCanvasItem::toMapCoordinates(QPoint(me->x(), me->y()));
    QgsPointXY p2 = QgsMapCanvasItem::toMapCoordinates(QPoint(me->x()+50, me->y()+50));  // 
    length = 12345.0;
    QgsDistanceArea da;
    // QgsCoordinateReferenceSystem new_crs = layer[0]->layer()->crs();
    //da.setSourceCrs(new_crs);
    length = da.measureLine(p1, p2);
    QMessageBox::warning(0, "Message", QString("MyCanvas::MyMouseReleaseEvent\n(p1,p2): (%1, %2), (%3, %4), length: %5")
        .arg(QString::number(p1.x())).arg(QString::number(p1.y()))
        .arg(QString::number(p2.x())).arg(QString::number(p2.y()))
                .arg(QString::number(length))
    );
    if (listener != NULL)
    {
        listener->onMouseUp(wx(me->x()), wy(me->y()), (AbstractCanvasListener::ButtonState) me->button() );
    }
    mMapCanvas->update();
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyKeyPressEvent( QKeyEvent* ke)
{
    if (ke->modifiers() & Qt::ShiftModifier)
    {
        int pressed_key = ke->key() & Qt::ShiftModifier;
        {
            QMessageBox::warning(0, "Message", QString(tr("MyCanvas::MyKeyPressEvent: %1").arg(ke->key())));
        }
    }

    if (listener != NULL)
    {
		listener->onKeyDown((AbstractCanvasListener::KeyCode) ke->key(), (AbstractCanvasListener::KeyboardModifier) int(ke->modifiers()));
        ke->accept();
    }
}
//
//-----------------------------------------------------------------------------
//
void MyCanvas::MyKeyReleasedEvent( QKeyEvent * ke)
{
    if (listener != NULL)
    {
        listener->onKeyUp((AbstractCanvasListener::KeyCode) ke->key(), (AbstractCanvasListener::KeyboardModifier) int(ke->modifiers()));
        ke->accept();
    }
}

//
//=============================================================================
//


void MyCanvas::InitDrawEachCaches(void) // debug utility
{
    /* Draw each cache separately */
    vb = new QVBoxLayout(mMapCanvas);
    for (int i = 0; i<NR_CACHES; i++) {
        label[i] = new QLabel(mMapCanvas);
        label[i]->setScaledContents(true);
        newCache(i);
    }
    for (int i = 0; i<NR_CACHES; i++) {
        if (i == 0) cacheArray[i]->fill(Qt::yellow);
        if (i == 1) cacheArray[i]->fill(Qt::yellow);
        if (i == 2) cacheArray[i]->fill(Qt::yellow);
        if (i == 3) cacheArray[i]->fill(Qt::yellow);
        if (i == 4) cacheArray[i]->fill(Qt::yellow);
        if (i == 5) cacheArray[i]->fill(Qt::yellow);
        //if (i==6) cacheArray[i]->fill(Qt::green); // already set in function newcache
        label[i]->setPixmap(QPixmap::fromImage(*cacheArray[i])); // consider QGraphicsScene
        label[i]->setFrameStyle(QFrame::Panel);
        vb->addWidget(label[i]);
    }

    //  sprintf(title, "%s %d %s %d", "Source: ", sourceCacheIndex,"    Destination: ", destCacheIndex);
    w = new QWidget(mMapCanvas);
    w->setWindowTitle("Pixmaps");
    w->setLayout(vb);
    w->setMaximumWidth(250);
    w->setMaximumHeight(850);
    w->show();
}
//-----------------------------------------------------------------------------

void MyCanvas::DrawEachCaches(void) // debug utility
{
    // Draw each cache separately on the canvas
    for (int i = 0; i<NR_CACHES; i++)
    {
        vb->removeWidget(label[i]);
        label[i]->setPixmap(QPixmap::fromImage(*cacheArray[i])); // consider QGraphicsView
        vb->addWidget(label[i]);
    }
    w->setLayout(vb);
    w->update();
}
