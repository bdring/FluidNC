// Copyright (c) 2024 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

void printIndividual(double individual[]);
void printPopulation(double population[][6]);
void assign(int index, double population[][6], double individual[]);
void mutate(int index, double population[][6],double maxMutation);
double findPointClosestToArcsDist(double measurement[], double individual[]);
void evaluateFitness(double individual[], double measurements[][4]);
void computeCalibration(double measurements[][4], double result[6], void (*webPrint) (double arg1),double tlX,double tlY, double trX, double trY, double brX, double tlZ, double trZ, double blZ, double brZ);
double myAbs(double x);
