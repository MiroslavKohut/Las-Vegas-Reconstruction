/*
 * FastGrid.cpp
 *
 *  Created on: 22.10.2008
 *      Author: Thomas Wiemann
 */

#include "FastGrid.h"

//Each box corner in the grid is shared with 7 other boxes.
//To find an already existing corner, these boxes have to
//be checked. The following table holds the information where
//to look for a given corner. The coding is as follows:
//
//Table row = query vertex
//
//Each row consists of 7 quadruples. The first three numbers
//indicate, how the indices in x- y- and z-direction have to
//be modified. The fourth entry is the vertex of the box
//correspondig to the modified indices.
//
//Example: index_x = 10, index_y = 7, index_z = 5
//
//Query vertex = 5
//
//First quadruple: {+1, 0, +1, 0}
//
//Indices pointing to the nb-box: 10 + 1, 7 + 0, 5 + 1.
//--> The first shared vertex is vertex number 0 of the box in position
//(11, 7, 6) of the grid.
//
//Simple isn't it?

const static int shared_vertex_table[8][28] = {
	{-1, 0, 0, 1, -1, -1, 0, 2,  0, -1, 0, 3, -1,  0, -1, 5, -1, -1, -1, 6,  0, -1, -1, 7,  0,  0, -1, 4},
	{ 1, 0, 0, 0,  1, -1, 0, 3,  0, -1, 0, 2,  0,  0, -1, 5,  1,  0, -1, 4,  1, -1, -1, 7,  0, -1, -1, 6},
	{ 1, 1, 0, 0,  0,  1, 0, 1,  1,  0, 0, 3,  1,  1, -1, 4,  0,  1, -1, 5,  0,  0, -1, 6,  1,  0, -1, 7},
	{ 0, 1, 0, 0, -1,  1, 0, 1, -1,  0, 0, 2,  0,  1, -1, 4, -1,  1, -1, 5, -1,  0, -1, 6,  0,  0, -1, 7},
	{ 0, 0, 1, 0, -1,  0, 1, 1, -1, -1, 1, 2,  0, -1,  1, 3, -1,  0,  0, 5, -1, -1,  0, 6,  0, -1,  0, 7},
	{ 1, 0, 1, 0,  0,  0, 1, 1,  0, -1, 1, 2,  1, -1,  1, 3,  1,  0,  0, 4,  0, -1,  0, 6,  1, -1,  0, 7},
	{ 1, 1, 1, 0,  0,  1, 1, 1,  0,  0, 1, 2,  1,  0,  1, 3,  1,  1,  0, 4,  0,  1,  0, 5,  1,  0,  0, 7},
	{ 0, 1, 1, 0, -1,  1, 1, 1, -1,  0, 1, 2,  0,  0,  1, 3,  0,  1,  0, 4, -1,  1,  0, 5, -1,  0,  0, 6}
};


//This table states where each coordinate of a box vertex is relatively
//to the box center
const static int box_creation_table[8][3] = {
	{-1, -1, -1},
	{ 1, -1, -1},
	{ 1,  1, -1},
	{-1,  1, -1},
	{-1, -1,  1},
	{ 1, -1,  1},
	{ 1,  1,  1},
	{-1,  1,  1}
};

FastGrid::FastGrid(string filename, float vs) {

	voxelsize = vs;
	number_of_points = 0;

	readPoints(filename);

	interpolator = new StannInterpolator(points, number_of_points, 10.0, 100, 100.0);

	calcIndices();
	createGrid();
	calcQueryPointValues();
	createMesh();
}

FastGrid::~FastGrid() {
	annDeallocPts(points);

	hash_map<int, FastBox*>::iterator it;
	for(it = cells.begin(); it != cells.end(); it++) delete it->second;
}

int  FastGrid::findQueryPoint(int position, int x, int y, int z){

	int n_x, n_y, n_z, q_v, offset;

	for(int i = 0; i < 7; i++){
		offset = i * 4;
		n_x = x + shared_vertex_table[position][offset];
		n_y = y + shared_vertex_table[position][offset + 1];
		n_z = z + shared_vertex_table[position][offset + 2];
		q_v = shared_vertex_table[position][offset + 3];

		int hash = hashValue(n_x, n_y, n_z);
		hash_map<int, FastBox*>::iterator it;
		it = cells.find(hash);
		if(it != cells.end()){
			FastBox* b = it->second;
			if(b->vertices[q_v] != -1) return b->vertices[q_v];
		}
	}

	return -1;

}

void FastGrid::createGrid(){

	//Create Grid
	cout << "##### Creating Grid..." << endl;

	//Current indices
	int index_x, index_y, index_z;
	int hash_value;

	float vsh = voxelsize / 2.0;

	//Iterators
	hash_map<int, FastBox*>::iterator it;
	hash_map<int, FastBox*>::iterator neighbour_it;

	int global_index = 0;
	int current_index = 0;

	int dx, dy, dz;

	for(int i = 0; i < number_of_points; i++){
		index_x = calcIndex((points[i][0] - bounding_box.v_min.x) / voxelsize);
		index_y = calcIndex((points[i][1] - bounding_box.v_min.y) / voxelsize);
		index_z = calcIndex((points[i][2] - bounding_box.v_min.z) / voxelsize);


		for(int j = 0; j < 8; j++){

			dx = HGCreateTable[j][0];
			dy = HGCreateTable[j][1];
			dz = HGCreateTable[j][2];

			hash_value = hashValue(index_x + dx, index_y + dy, index_z +dz);
			it = cells.find(hash_value);
			if(it == cells.end()){
				//Calculate box center
				Vertex box_center = Vertex(
						(index_x + dx) * voxelsize + bounding_box.v_min.x,
						(index_y + dy) * voxelsize + bounding_box.v_min.y,
						(index_z + dz) * voxelsize + bounding_box.v_min.z);

				//Create new box
				FastBox* box = new FastBox;

				//Setup box
				for(int k = 0; k < 8; k++){
					current_index = findQueryPoint(k, index_x + dx, index_y + dy, index_z + dz);
					if(current_index != -1) box->vertices[k] = current_index;
					else{
						Vertex position(box_center.x + box_creation_table[k][0] * vsh,
								box_center.y + box_creation_table[k][1] * vsh,
								box_center.z + box_creation_table[k][2] * vsh);

						query_points.push_back(QueryPoint(position));

						box->vertices[k] = global_index;
						global_index++;

					}
				}
				cells[hash_value] = box;
			}
		}
	}
	cout << "##### Finished Grid Creation. Number of generated cells:        " << cells.size() << endl;
	cout << "##### Finished Grid Creation. Number of generated query points: " << query_points.size() << endl;
}

void FastGrid::calcQueryPointValues(){

	for(size_t i = 0; i < query_points.size(); i++){
		if(i % 10000 == 0) cout << "##### Calculating distance values: " << i << " / " << query_points.size() << endl;
		QueryPoint p = query_points[i];
		ColorVertex v = ColorVertex(p.position, 0.0f, 1.0f, 0.0f);
		p.distance = interpolator->distance(v);
		query_points[i] = p;
	}

}

void FastGrid::createMesh(){

//	cout << "##### Creating Mesh..." << endl;
//
//	hash_map<int, Box*>::iterator it;
//	Box* b;
//	int global_index = 0;
//	int c = 0;
//
//	for(it = cells.begin(); it != cells.end(); it++){
//		if(c % 1000 == 0) cout << "##### Iterating Cells... " << c << " / " << cells.size() << endl;;
//		b = it->second;
//		global_index = b->getApproximation(global_index,
//				mesh,
//				interpolator);
//		c++;
//	}
//
//	mesh.printStats();
//	mesh.finalize();


	cout << "##### Creating Mesh..." << endl;

	hash_map<int, FastBox*>::iterator it;
	FastBox* b;
	int global_index = 0;
	int c = 0;

	for(it = cells.begin(); it != cells.end(); it++){
		if(c % 1000 == 0) cout << "##### Iterating Cells... " << c << " / " << cells.size() << endl;;
		b = it->second;
		global_index = b->calcApproximation(query_points, mesh, global_index);
		c++;
	}

	mesh.printStats();
	mesh.finalize();
	mesh.save("mesh.ply");
}


void FastGrid::calcIndices(){

	float max_size = max(max(bounding_box.x_size, bounding_box.y_size), bounding_box.z_size);

	//Save needed grid parameters
	max_index = (int)ceil( (max_size + 5 * voxelsize) / voxelsize);
	max_index_square = max_index * max_index;

	max_index_x = (int)ceil(bounding_box.x_size / voxelsize) + 1;
	max_index_y = (int)ceil(bounding_box.y_size / voxelsize) + 2;
	max_index_z = (int)ceil(bounding_box.z_size / voxelsize) + 3;

}


void FastGrid::readPoints(string filename){

	ifstream in(filename.c_str());

	//Vector to tmp-store points in file
	vector<BaseVertex> pts;

	//Read all points. Save maximum and minimum dimensions and
	//calculate maximum indices.
	int c = 0;

	//Get number of data fields to ignore
	int number_of_dummys = getFieldsPerLine(filename) - 3;

	//Point coordinates
	float x, y, z, dummy;

	//Read file
	while(in.good()){
		in >> x >> y >> z;
		for(int i = 0; i < number_of_dummys; i++){
			in >> dummy;
		}

		bounding_box.expand(x, y, z);
		pts.push_back(BaseVertex(x,y,z));
		c++;

		if(c % 10000 == 0) cout << "##### Reading Points... " << c << endl;
	}

	cout << "##### Finished Reading. Number of Data Points: " << pts.size() << endl;


	//Create ANNPointArray
	cout << "##### Creating ANN Points " << endl;
	points = annAllocPts(c, 3);

	for(size_t i = 0; i < pts.size(); i++){
		points[i][0] = pts[i].x;
		points[i][1] = pts[i].y;
		points[i][2] = pts[i].z;
	}

	pts.clear();

	number_of_points = c;
}

int FastGrid::getFieldsPerLine(string filename){

  ifstream in(filename.c_str());

  //Get first line from file
  char first_line[1024];
  in.getline(first_line, 1024);
  in.close();

  //Get number of blanks
  int c = 0;
  char* pch = strtok(first_line, " ");
  while(pch != NULL){
    c++;
    pch = strtok(NULL, " ");
  }

  in.close();

  return c;
}

void FastGrid::writeGrid(){
	cout << "##### Writing 'grid.hg'" << endl;

	ofstream out("grid.hg");

	out << number_of_points << endl;

	for(int i = 0; i < number_of_points; i++){
		out << points[i][0] << " " <<  points[i][1] << " " << points[i][2] << endl;
	}

	hash_map<int, FastBox*>::iterator it;
	FastBox* box;

	for(it = cells.begin(); it != cells.end(); it++){
		box = it->second;

		for(int i= 0; i < 8; i++){
			QueryPoint qp = query_points[box->vertices[i]];
			Vertex v = qp.position;
			out << v.x << " " << v.y << " " << v.z << " " << 0.0 << " " << 1.0 << " " << 00. << endl;
		}
	}
}
