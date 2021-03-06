
ConvLayer::ConvLayer (int netI, int s) : Layer(netI, s) {
    netInstance = netI;
    size = s;
    type = "Conv";
    hasActivation = false;
}

ConvLayer::~ConvLayer (void) {
    for (int f=0; f<filters.size(); f++) {
        delete filters[f];
    }
}

void ConvLayer::assignNext (Layer* l) {
    nextLayer = l;
}

void ConvLayer::assignPrev (Layer* l) {
    prevLayer = l;

    for (int f=0; f<size; f++) {
        filters.push_back(new Filter());
    }
}

void ConvLayer::init (int layerIndex) {

    Network* net = Network::getInstance(netInstance);
    for (int f=0; f<filters.size(); f++) {

        // Weights
        for (int c=0; c<channels; c++) {
            std::vector<std::vector<double> > weightsMap;

            for (int r=0; r<filterSize; r++) {
                weightsMap.push_back(net->weightInitFn(netInstance, layerIndex, filterSize));
            }

            filters[f]->weights.push_back(weightsMap);
        }

        filters[f]->activationMap = NetUtil::createVolume<double>(1, outMapSize, outMapSize, 0)[0];
        filters[f]->errorMap = NetUtil::createVolume<double>(1, outMapSize, outMapSize, 0)[0];
        filters[f]->bias = 1;

        if (net->dropout != 1) {
            filters[f]->dropoutMap = NetUtil::createVolume<bool>(1, outMapSize, outMapSize, 0)[0];
        }

        filters[f]->init(netInstance);
    }
}

void ConvLayer::forward (void) {

    Network* net = Network::getInstance(netInstance);
    std::vector<double> activations = NetUtil::getActivations(prevLayer);

    for (int f=0; f<filters.size(); f++) {

        filters[f]->sumMap = NetUtil::convolve(activations, zeroPadding, filters[f]->weights, channels, stride, filters[f]->bias);

        for (int sumY=0; sumY<filters[f]->sumMap.size(); sumY++) {
            for (int sumX=0; sumX<filters[f]->sumMap.size(); sumX++) {

                if (net->dropout != 1) {
                    filters[f]->dropoutMap[sumY][sumX] = (double) rand() / (RAND_MAX) > net->dropout;
                }

                if (net->dropout != 1 && net->isTraining && filters[f]->dropoutMap[sumY][sumX]) {
                    filters[f]->activationMap[sumY][sumX] = 0;

                } else if (hasActivation) {

                    filters[f]->activationMap[sumY][sumX] = activationC(filters[f]->sumMap[sumY][sumX], false, filters[f]) / net->dropout;

                } else {
                    filters[f]->activationMap[sumY][sumX] = filters[f]->sumMap[sumY][sumX];
                }
            }
        }
    }
}

void ConvLayer::backward () {

    if (nextLayer->type == "FC") {

        // For each filter, build the errorMap from the weighted neuron errors in the next FCLayer corresponding to each value in the activation map
        for (int f=0; f<filters.size(); f++) {

            for (int emY=0; emY<filters[f]->errorMap.size(); emY++) {
                for (int emX=0; emX<filters[f]->errorMap.size(); emX++) {

                    int weightI = f * outMapSize*outMapSize + emY * filters[f]->errorMap.size() + emX;

                    for (int n=0; n < nextLayer->neurons.size(); n++) {
                        filters[f]->errorMap[emY][emX] += nextLayer->neurons[n]->error * nextLayer->neurons[n]->weights[weightI];
                    }
                }
            }
        }

    } else if (nextLayer->type == "Conv") {

        for (int f=0; f<filters.size(); f++) {
            filters[f]->errorMap = NetUtil::buildConvErrorMap(outMapSize + nextLayer->zeroPadding*2, nextLayer, f);
        }

    } else {

        for (int f=0; f<filters.size(); f++) {

            for (int r=0; r<filters[f]->errorMap.size(); r++) {
                for (int v=0; v<filters[f]->errorMap.size(); v++) {
                    filters[f]->errorMap[r][v] = nextLayer->errors[f][r][v];
                }
            }
        }
    }

    // Apply derivative to each error value
    for (int f=0; f<filters.size(); f++) {
        for (int row=0; row<filters[f]->errorMap.size(); row++) {
            for (int col=0; col<filters[f]->errorMap[0].size(); col++) {

                if (filters[f]->dropoutMap.size() && filters[f]->dropoutMap[row][col]) {
                    filters[f]->errorMap[row][col] = 0;
                } else if (hasActivation) {
                    filters[f]->errorMap[row][col] *= activationC(filters[f]->sumMap[row][col], true, filters[f]);
                }
            }
        }
    }

    NetUtil::buildConvDWeights(this);
}

void ConvLayer::resetDeltaWeights (void) {

    for (int f=0; f<filters.size(); f++) {

        filters[f]->deltaBias = 0;

        for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
            for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {
                    filters[f]->deltaWeights[c][r][v] = 0;
                }
            }
        }

        for (int row=0; row<filters[f]->errorMap.size(); row++) {
            for (int col=0; col<filters[f]->errorMap.size(); col++) {
                filters[f]->errorMap[row][col] = 0;
            }
        }

        if (filters[f]->dropoutMap.size()) {
            for (int r=0; r<filters[f]->dropoutMap.size(); r++) {
                for (int c=0; c<filters[f]->dropoutMap[0].size(); c++) {
                    filters[f]->dropoutMap[r][c] = false;
                }
            }
        }
    }
}

void ConvLayer::applyDeltaWeights (void) {

    Network* net = Network::getInstance(netInstance);

    for (int f=0; f<filters.size(); f++) {
        for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
            for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {
                    if (net->l2) net->l2Error += 0.5 * net->l2 * pow(filters[f]->weights[c][r][v], 2);
                    if (net->l1) net->l1Error += net->l1 * fabs(filters[f]->weights[c][r][v]);
                }
            }
        }
    }

    // Function pointers are far too slow, for this
    // Using code repetitive switch statements makes a substantial perf difference
    // Doesn't mean I'm happy about it :(
    switch (net->updateFnIndex) {
        case 0: // vanilla
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                + net->l2 * filters[f]->weights[c][r][v]
                                + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::vanillaupdatefn(netInstance, filters[f]->weights[c][r][v], regularized);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::vanillaupdatefn(netInstance, filters[f]->bias, filters[f]->deltaBias);
            }
            break;
        case 1: // gain
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                                            + net->l2 * filters[f]->weights[c][r][v]
                                                            + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::gain(netInstance, filters[f]->weights[c][r][v], regularized, filters[f], c, r, v);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::gain(netInstance, filters[f]->bias, filters[f]->deltaBias, filters[f], -1, -1, -1);
            }
            break;
        case 2: // adagrad
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                                            + net->l2 * filters[f]->weights[c][r][v]
                                                            + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::adagrad(netInstance, filters[f]->weights[c][r][v], regularized, filters[f], c, r, v);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::adagrad(netInstance, filters[f]->bias, filters[f]->deltaBias, filters[f], -1, -1, -1);
            }
            break;
        case 3: // rmsprop
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                                            + net->l2 * filters[f]->weights[c][r][v]
                                                            + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::rmsprop(netInstance, filters[f]->weights[c][r][v], regularized, filters[f], c, r, v);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::rmsprop(netInstance, filters[f]->bias, filters[f]->deltaBias, filters[f], -1, -1, -1);
            }
            break;
        case 4: // adam
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                                            + net->l2 * filters[f]->weights[c][r][v]
                                                            + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::adam(netInstance, filters[f]->weights[c][r][v], regularized, filters[f], c, r, v);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::adam(netInstance, filters[f]->bias, filters[f]->deltaBias, filters[f], -1, -1, -1);
            }
            break;
        case 5: // adadelta
            for (int f=0; f<filters.size(); f++) {
                for (int c=0; c<filters[f]->deltaWeights.size(); c++) {
                    for (int r=0; r<filters[f]->deltaWeights[0].size(); r++) {
                        for (int v=0; v<filters[f]->deltaWeights[0][0].size(); v++) {

                            double regularized = (filters[f]->deltaWeights[c][r][v]
                                                            + net->l2 * filters[f]->weights[c][r][v]
                                                            + net->l1 * (filters[f]->weights[c][r][v] > 0 ? 1 : -1)) / net->miniBatchSize;

                            filters[f]->weights[c][r][v] = NetMath::adadelta(netInstance, filters[f]->weights[c][r][v], regularized, filters[f], c, r, v);

                            if (net->maxNorm) net->maxNormTotal += filters[f]->weights[c][r][v] * filters[f]->weights[c][r][v];
                        }
                    }
                }
                filters[f]->bias = NetMath::adadelta(netInstance, filters[f]->bias, filters[f]->deltaBias, filters[f], -1, -1, -1);
            }
            break;
    }

    if (net->maxNorm) {
        net->maxNormTotal = sqrt(net->maxNormTotal);
        NetMath::maxNorm(netInstance);
    }
}