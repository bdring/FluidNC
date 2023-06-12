#include "calibration.h"
#include <Arduino.h>

float _tlZ;
float _trZ;
float _blZ;
float _brZ;

void printIndividual(double individual[]){
    Serial.print("| ");
    for(int i = 0; i< 6; i++){
        Serial.print(individual[i]);
        Serial.print(" | ");
    }
    Serial.print("\n");
}

void printPopulation(double population[][6]){
    
    Serial.println("Population: ");
    for(int i = 0; i < 50; i++){
        Serial.print(i);
        Serial.print("  ");
        printIndividual(population[i]);
    }
}

//Assigns an individual to the population
void assign(int index, double population[][6], double individual[]){
    for(int i = 0; i< 6; i++){
        population[index][i] = individual[i];
    }
}

//Mutates an individual within the population
void mutate(int index, double population[][6],double maxMutation){
    for(int i = 0; i< 5; i++){
        population[index][i] = population[index][i] + (random(-100.0*maxMutation,100*maxMutation)/100.0);
    }
}

double myAbs(double x){
    if(x < 0){
        return -1.0*x;
    }
    else{
        return x;
    }
}

double findDistanceToArc(double px,double py,double r,double ax,double ay){
    double i1 = px - ax;
    double i2 = py - ay;
    
    double dist = myAbs(sqrt((i1*i1)+(i2*i2))-r);
    return dist;
}

//Modify the radius to acocunt for height change
double inPlaneRadius(double radius, double deltaZ){
    return sqrt(radius*radius - deltaZ*deltaZ);
}

//Find the avg dist from xy point to all four arcs
double distPointToArcs(double x,double y,double measurement[],double individual[]){
    
    //Compute dist to top left arc
    double tlDist = findDistanceToArc(x, y, inPlaneRadius(measurement[0], _tlZ), individual[0], individual[1]);
    
    //Compute dist to tr arc
    double trDist = findDistanceToArc(x, y, inPlaneRadius(measurement[1], _trZ), individual[2], individual[3]);
    
    //Compute dist to bl arc
    double blDist = findDistanceToArc(x, y, inPlaneRadius(measurement[2], _blZ), 0, 0);
    
    //Compute dist to br arc
    double brDist = findDistanceToArc(x, y, inPlaneRadius(measurement[3], _brZ), individual[4], 0);
    
    //Return the average
    return (tlDist + trDist + blDist + brDist)/4.0;
}

void evaluateDistandMin(double currentBest[], double x,double y,double measurement[],double individual[]){
    double newDist = distPointToArcs(x,y,measurement, individual);
    if(newDist < currentBest[2]){
        currentBest[0] = x;
        currentBest[1] = y;
        currentBest[2] = newDist;
    }
}

//Walks the gradient recursively to find the closest point
void walkClosenessGradient(double x, double y, double stepSize, double measurement[], double individual[], double finalPoint[], int recursionLevel){
    
    recursionLevel = recursionLevel + 1;
    double closestPoint[] = {0,0,10000};
    
    //0,0
    evaluateDistandMin(closestPoint,x,y,measurement, individual);
    //0+
    evaluateDistandMin(closestPoint,x,y+stepSize,measurement, individual);
    //++
    evaluateDistandMin(closestPoint,x+stepSize,y+stepSize,measurement, individual);
    //+0
    evaluateDistandMin(closestPoint,x+stepSize,y,measurement, individual);
    //+-
    evaluateDistandMin(closestPoint,x+stepSize,y-stepSize,measurement, individual);
    //0-
    evaluateDistandMin(closestPoint,x,y-stepSize,measurement, individual);
    //--
    evaluateDistandMin(closestPoint,x-stepSize,y-stepSize,measurement, individual);
    //-0
    evaluateDistandMin(closestPoint,x-stepSize,y,measurement, individual);
    //-+
    evaluateDistandMin(closestPoint,x-stepSize,y+stepSize,measurement, individual);
    
    //If that point is the center return
    if(closestPoint[0] == x && closestPoint[1] == y){
        finalPoint[0] = x;
        finalPoint[1] = y;
        finalPoint[2] = closestPoint[2];
    }
    else if(recursionLevel > 15){ //Don't allow deeper recursion to crash the stack
        finalPoint[0] = x;
        finalPoint[1] = y;
        finalPoint[2] = closestPoint[2];
    }
    else{  //Recursively continue walking the gradient
        walkClosenessGradient(closestPoint[0], closestPoint[1], stepSize, measurement, individual, finalPoint, recursionLevel);
    }
}

//Find the dist from the closest point to the arcs
double findPointClosestToArcsDist(double measurement[], double individual[]){
    
    double currentClosestPoint[3];
    
    //Use the middle as the initial guess
    walkClosenessGradient(individual[2]/2,individual[3]/2, 100, measurement, individual, currentClosestPoint, 0);
    
    //Second pass
    walkClosenessGradient(currentClosestPoint[0],currentClosestPoint[1], 10, measurement, individual, currentClosestPoint, 0);
    
    //Third pass
    walkClosenessGradient(currentClosestPoint[0],currentClosestPoint[1], 1, measurement, individual, currentClosestPoint, 0);
    
    //Fourth pass
    walkClosenessGradient(currentClosestPoint[0],currentClosestPoint[1], .1, measurement, individual, currentClosestPoint, 0);
    
    return myAbs(currentClosestPoint[2]);
}

//Evaluate the fitness of an individual
void evaluateFitness(double individual[], double measurements[][4]){
    
    double m1Fitness = findPointClosestToArcsDist(measurements[0], individual);
    double m2Fitness = findPointClosestToArcsDist(measurements[1], individual);
    double m3Fitness = findPointClosestToArcsDist(measurements[2], individual);
    double m4Fitness = findPointClosestToArcsDist(measurements[3], individual);
    double m5Fitness = findPointClosestToArcsDist(measurements[4], individual);
    double m6Fitness = findPointClosestToArcsDist(measurements[5], individual);
    double m7Fitness = findPointClosestToArcsDist(measurements[6], individual);
    double m8Fitness = findPointClosestToArcsDist(measurements[7], individual);
    double m9Fitness = findPointClosestToArcsDist(measurements[8], individual);
    
    individual[5] = (m1Fitness + m2Fitness + m3Fitness + m4Fitness + m5Fitness + m6Fitness + m7Fitness + m8Fitness + m9Fitness)/9.0;
}

int sort(const void *pa, const void *pb){
    
    // Need to recast the void *
    double* ia = (double*)pa;
    double* ib = (double*)pb;
    
    double a = myAbs(ia[5]);
    double b = myAbs(ib[5]);
    
    // The comparison
    return a > b ? 1 : (a < b ? -1 : 0);
    
}

void sortPopulation(double population[][6]){
    qsort(population, 50, sizeof(population[0]), sort);
}

void cullAndBreed(double population[][6],double stepSize){
    //Population is sorted at this point
    
    for(int i = 9; i < 50; i++){
        assign(i, population, population[random(0,9)]);
        mutate(i, population, stepSize);
    }
}

void evolve(double population[][6], double measurements[][4], double stepSize, double targetFitness, int timeout, void (*webPrint) (double arg1)){
    int i = 0;
    double lastVal = 0;
    int numberOfRepitions = 0; //The number of times the same value has been found
    while(population[0][5] > targetFitness){
        
        //Sort the population
        sortPopulation(population);
        
        if(population[0][5] == lastVal){
            numberOfRepitions++;
        }
        else{
            numberOfRepitions = 0;
        }
        lastVal = population[0][5];
        if(numberOfRepitions > timeout){
            Serial.println("Stuck");
            break;
        }
        
        Serial.print("Fittest: ");
        Serial.print(population[0][5]);
        Serial.print("      gen: ");
        Serial.print(i);
        Serial.print("    step: ");
        Serial.print(stepSize);
        Serial.print("    reps: ");
        Serial.println(numberOfRepitions);
        
        
        webPrint(population[0][5]);
        
        //Breed the best (Start with just mutating them)
        cullAndBreed(population, stepSize);
        
        //Evaluate the fitness of the new individuals
        for(int i = 0; i < 50; i++){
            evaluateFitness(population[i], measurements);
        }
        
        //Repeat until the fitness function is within some threshold or timeout
        
        i++;
        if(i > 1000){
            Serial.println("Max number of iterations exceded");
            break;
        }
    }
}

//Compute the calibration
void computeCalibration(double measurements[][4], double result[6], void (*webPrint) (double arg1),double tlX,double tlY, double trX, double trY, double brX, double tlZ, double trZ, double blZ, double brZ){
    Serial.println("Beginning to compute calibration");
    
    _tlZ = tlZ;
    _trZ = trZ;
    _blZ = blZ;
    _brZ = brZ;
    
    // Establish initial guesses for the corners
    double initialIndividual[] = {tlX, tlY, trX, trY, brX, 10000};
    
    // Build a population
    double population[50][6];
    for(int i = 0; i < 50; i++){
        assign(i, population, initialIndividual);
    }
    
    
    //Mutate the population
    for(int i = 1; i < 50; i++){
        mutate(i, population, 2);
    }
    
    //Compute fitness of individuals
    for(int i = 0; i < 50; i++){
        evaluateFitness(population[i], measurements);
    }
    
    //Evolve the population 
    webPrint(5);
    evolve(population, measurements,  5, .25, 3, webPrint);
    webPrint(.5);
    evolve(population, measurements, .5, .25, 3, webPrint);
    webPrint(.1);
    evolve(population, measurements, .1, .01, 3, webPrint);
    webPrint(.01);
    evolve(population, measurements, .01, .01, 3, webPrint);
    
    sortPopulation(population);
    
    //Pass back the result
    result[0] = population[0][0];
    result[1] = population[0][1];
    result[2] = population[0][2];
    result[3] = population[0][3];
    result[4] = population[0][4];
    result[5] = population[0][5];
}
